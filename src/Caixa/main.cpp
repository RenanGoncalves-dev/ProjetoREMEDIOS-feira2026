#include <Arduino.h>
#include <Stepper.h>
#include <Wire.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// ============================================================================
// PROJETO E.L.O.S. - CAIXA INTELIGENTE DISPENSADORA DE MEDICAMENTOS
// Firmware Oficial: ESP32 + Bluetooth Low Energy (BLE) + OLED + RFID RC522
// ============================================================================

// ============================================================================
// 1. CONFIGURAÇÕES DE OPERAÇÃO
// ============================================================================
// 0 = Bluetooth Low Energy (BLE UART Nordic) -> 100% Compatível com App React/Android
// 1 = Bluetooth Clássico (SerialBT SPP)
#define USAR_BLUETOOTH_CLASSICO 0

// 0 = Standby da Pulseira (pronto para expansão física)
// 1 = Ativa envio via ESP-NOW para XIAO ESP32
#define ATIVAR_PULSEIRA 0

#define NOME_BLUETOOTH "ELOS_Caixa"

// ============================================================================
// 2. MAPA OFICIAL DE PINAGEM DO HARDWARE (ESP32 DevKit)
//    Pinagem do RFID CONFIRMADA pelo Scanner de Diagnóstico (env:scanner)
// ============================================================================
// MOTOR DE PASSO 28BYJ-48 + DRIVER ULN2003
#define IN1 5   // GPIO 5  -> Driver IN1
#define IN2 18  // GPIO 18 -> Driver IN2
#define IN3 19  // GPIO 19 -> Driver IN3
#define IN4 21  // GPIO 21 -> Driver IN4

// DISPLAY OLED 0.96" I2C (SSD1306 128x64)
#define OLED_SDA 14 // GPIO 14 -> Display SDA
#define OLED_SCL 27 // GPIO 27 -> Display SCL
#define OLED_ENDERECO 0x3C

// LEITOR RFID RC522 (LIBERAÇÃO DE EMERGÊNCIA / CUIDADOR)
// ----------------------------------------------------------------------------
// Pino no RC522 | Cor do Fio    | Pino no ESP32 | Função / Observação
// ----------------------------------------------------------------------------
// SDA (SS)      | Fio Azul      | D25 (GPIO 25) | SPI Chip Select (Saída do ESP32)
// SCK           | Fio Roxo      | D33 (GPIO 33) | SPI Clock       (Saída do ESP32)
// MOSI          | Fio Verde     | D26 (GPIO 26) | SPI Master Out  (Saída do ESP32 - NÃO USAR D34!)
// MISO          | Fio Amarelo   | D35 (GPIO 35) | SPI Master In   (Entrada no ESP32 - D35 é GPI)
// IRQ           | (não ligado)  | NÃO CONECTAR  | Não utilizado
// GND           | Fio Preto     | GND           | Terra Comum
// RST           | Fio Marrom    | D32 (GPIO 32) | Reset do Leitor (Saída do ESP32)
// 3.3V          | Fio Vermelho  | 3V3           | Alimentação 3.3V (NUNCA ligar no 5V!)
// ----------------------------------------------------------------------------
#define RFID_SDA  25
#define RFID_SCK  33
#define RFID_MOSI 26
#define RFID_MISO 35
#define RFID_RST  32

// BUZZER DE 3 PERNAS (MÓDULO: VCC / GND / I/O)
// ----------------------------------------------------------------------------
// Pino no Módulo | Pino no ESP32 | Observação
// ----------------------------------------------------------------------------
// VCC (+)        | 3V3           | Use 3V3 (no 5V, módulos acionados em LOW ficam tocando sem parar)
// GND (-)        | GND           | Terra comum
// I/O (S / SIG)  | D23 (GPIO 23) | Saída digital/PWM livre, não interfere no boot do ESP32
// ----------------------------------------------------------------------------
#define BUZZER_PIN 23

// 1 = Buzzer ATIVO (já tem oscilador interno, ex: KY-012) -> apita só com HIGH/LOW
// 0 = Buzzer PASSIVO (sem oscilador, ex: KY-006) -> precisa de PWM (toca melodias com notas)
#define BUZZER_ATIVO 1

// Nível lógico que LIGA o buzzer:
// HIGH = módulos comuns com transistor NPN (padrão)
// LOW  = módulos com transistor PNP (S8550). Se o buzzer apitar SEM PARAR ao ligar, troque para LOW.
#define BUZZER_NIVEL_LIGADO HIGH

// ============================================================================
// 3. PARÂMETROS DE DESEMPENHO E SEGURANÇA
// ============================================================================
#define MAX_AGENDAMENTOS         8
#define TAM_MAX_COMANDO          96      // Tamanho máximo de um comando recebido (bytes)
#define TAM_FILA_COMANDOS        8       // Comandos BLE enfileirados sem perda
#define PASSOS_POR_DOSE          276     // Passos do motor por compartimento (~45 graus, calibrado)
#define VELOCIDADE_MOTOR_RPM     12

const unsigned long COOLDOWN_RFID_MS        = 2500;  // Evita leituras repetidas do mesmo toque
const unsigned long INTERVALO_POLL_RFID_MS  = 80;    // Consulta o leitor ~12x/s (antes: todo loop)
const unsigned long INTERVALO_SAUDE_RFID_MS = 5000;  // Verifica/reconecta o RC522 a cada 5s
const unsigned long INTERVALO_TELA_MS       = 1000;  // Atualização do OLED
const unsigned long MAX_SEGUNDOS_AGENDA     = 1728000UL; // 20 dias (limite seguro do millis())

// ============================================================================
// 4. INSTÂNCIAS DOS DISPOSITIVOS
// ============================================================================
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Stepper myStepper(2048, IN1, IN3, IN2, IN4);
MFRC522 rfid(RFID_SDA, RFID_RST);
Preferences prefs;

// Fila thread-safe entre a task do Bluetooth e o loop() principal
QueueHandle_t filaComandos = nullptr;

// Acumulador de linhas de texto (Serial USB, BT Clássico e BLE)
struct BufferLinha {
  char dados[TAM_MAX_COMANDO];
  size_t len = 0;
};

#if USAR_BLUETOOTH_CLASSICO
  #include <BluetoothSerial.h>
  BluetoothSerial SerialBT;
  BufferLinha bufferBT;
#else
  #include <BLEDevice.h>
  #include <BLEServer.h>
  #include <BLEUtils.h>
  #include <BLE2902.h>

  #define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
  #define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Celular -> Caixa
  #define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // Caixa -> Celular
  #define MTU_LOCAL_BLE          185

  BLEServer* pServer = nullptr;
  BLECharacteristic* pTxCharacteristic = nullptr;
  volatile bool bleConectado = false;
  bool bleConectadoAnterior = false;
  BufferLinha bufferBLE; // Usado apenas dentro da task do Bluetooth
#endif

#if ATIVAR_PULSEIRA
  #include <WiFi.h>
  #include <esp_now.h>
#endif

// ============================================================================
// 5. ESTRUTURA PARA A PULSEIRA (EXPANSÃO FUTURA)
// ============================================================================
uint8_t macPulseira[] = {0x84, 0xFC, 0xE6, 0x00, 0xD3, 0x38};

typedef struct struct_message {
  int comando;
  int slotsRestantes;
} struct_message;

struct_message dadosPulseira;

void iniciarComunicacaoPulseira() {
#if ATIVAR_PULSEIRA
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macPulseira, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("[PULSEIRA] Interface ESP-NOW ativa.");
  }
#endif
}

void notificarPulseira(int codigoComando) {
  dadosPulseira.comando = codigoComando;
#if ATIVAR_PULSEIRA
  esp_now_send(macPulseira, (uint8_t*)&dadosPulseira, sizeof(dadosPulseira));
  Serial.printf("[PULSEIRA] Comando %d enviado via ESP-NOW.\n", codigoComando);
#else
  Serial.printf("[PULSEIRA - STANDBY] Evento %d pronto.\n", codigoComando);
#endif
}

// ============================================================================
// 6. ESTADO DA CAIXA: ESTOQUE, AGENDAMENTOS E PERIFÉRICOS
// ============================================================================
int capacidadeTotal = 7;
int slotsOcupados = 0;
bool emUso = false;

struct Agendamento {
  unsigned long alvoMillis;
  unsigned long repeteMillis;
  bool ativo;
  String descricao;
};
Agendamento listaAgendamentos[MAX_AGENDAMENTOS];

// Display OLED
bool oledOk = false;
bool msgAtiva = false;
unsigned long msgAte = 0;
unsigned long ultimoRefresh = 0;

// Leitor RFID
bool rfidOperacional = false;
unsigned long ultimoRfidMillis = 0;
unsigned long ultimoPollRfid = 0;
unsigned long ultimaSaudeRfid = 0;

// Buffer Serial USB
BufferLinha bufferSerial;

// Protótipos
void enviarStatusBT();
void enviarRespostaBT(const String& msg);
void updateDisplay(const String& linha1, const String& linha2);
void showMessage(const String& l1, const String& l2, unsigned long ms = 2500);

// ============================================================================
// 7. UTILITÁRIOS DE ENTRADA DE TEXTO
// ============================================================================
// Acumula um caractere; retorna true quando uma linha completa está em b.dados
bool acumularCaractere(BufferLinha& b, char c) {
  if (c == '\n' || c == '\r') {
    if (b.len == 0) return false;
    b.dados[b.len] = '\0';
    b.len = 0;
    return true;
  }
  if (b.len < TAM_MAX_COMANDO - 1) {
    b.dados[b.len++] = c;
  }
  return false;
}

// ============================================================================
// 8. PERSISTÊNCIA NA FLASH (NVS / PREFERENCES)
// ============================================================================
void salvarEstoqueNaFlash() {
  prefs.begin("elos", false);
  prefs.putInt("total", capacidadeTotal);
  prefs.putInt("ocupados", slotsOcupados);
  prefs.end();
}

void carregarEstoqueDaFlash() {
  prefs.begin("elos", true);
  capacidadeTotal = prefs.getInt("total", 7);
  slotsOcupados = prefs.getInt("ocupados", 0);
  prefs.end();
}

// ============================================================================
// 9. MOTOR, AGENDA E DISPLAY
// ============================================================================
void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

long restanteMs(int i) {
  return (long)(listaAgendamentos[i].alvoMillis - millis());
}

int proximoAgendamento() {
  int idx = -1;
  long menor = 0;
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    if (!listaAgendamentos[i].ativo) continue;
    long r = restanteMs(i);
    if (idx == -1 || r < menor) {
      idx = i;
      menor = r;
    }
  }
  return idx;
}

// Reagenda sem acumular atraso (mantém a cadência exata) ou encerra dose única
void reagendarOuDesativar(Agendamento& a) {
  if (a.repeteMillis > 0) {
    do {
      a.alvoMillis += a.repeteMillis;
    } while ((long)(a.alvoMillis - millis()) <= 0);
  } else {
    a.ativo = false;
  }
}

void formatarTempo(long ms, char* buf, size_t tam) {
  if (ms < 0) ms = 0;
  long s = ms / 1000;
  long h = s / 3600;
  long m = (s % 3600) / 60;
  long x = s % 60;
  if (h > 99) h = 99;
  snprintf(buf, tam, "%02ld:%02ld:%02ld", h, m, x);
}

bool bluetoothEstaConectado() {
#if USAR_BLUETOOTH_CLASSICO
  return SerialBT.hasClient();
#else
  return bleConectado;
#endif
}

void updateDisplay(const String& linha1, const String& linha2) {
  if (!oledOk) return; // Evita timeouts no I2C se o display estiver ausente
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println(linha1);
  display.setTextSize(2);
  display.setCursor(0, 30);
  display.println(linha2);
  display.display();
}

void showMessage(const String& l1, const String& l2, unsigned long ms) {
  updateDisplay(l1, l2);
  msgAtiva = true;
  msgAte = millis() + ms;
}

void telaPrincipal() {
  if (!oledOk) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Linha superior: Status Bluetooth + Status RFID
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(bluetoothEstaConectado() ? "BT:OK " : "BT:OFF ");
  display.print("| RFID:");
  display.print(rfidOperacional ? "OK" : "ERRO");

  // Linha central: Estoque de Doses
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print("Est:");
  display.print(slotsOcupados);
  display.print("/");
  display.print(capacidadeTotal);

  // Linha inferior: Próxima dose programada
  display.setTextSize(1);
  display.setCursor(0, 38);
  int p = proximoAgendamento();
  char buf[12];
  if (p < 0) {
    display.print("Sem agendamento");
    strcpy(buf, "--:--:--");
  } else {
    display.print("Proxima dose em:");
    formatarTempo(restanteMs(p), buf, sizeof(buf));
  }
  display.setTextSize(2);
  display.setCursor(0, 48);
  display.print(buf);
  display.display();
}

// ============================================================================
// 9.1 BUZZER (APITA TODA VEZ QUE UM REMÉDIO É LIBERADO, DE QUALQUER FORMA)
//     Execução NÃO-BLOQUEANTE: o loop continua rodando enquanto o buzzer toca
// ============================================================================
struct NotaBuzzer {
  uint16_t freq; // Hz (0 = silêncio). No buzzer ATIVO qualquer valor > 0 apenas liga.
  uint16_t ms;   // Duração da nota
};

// Sinal de "Remédio liberado": bip-bip-BIIP ... bip-bip-BIIP (~1,7 s)
const NotaBuzzer MELODIA_DOSE[] = {
  {2093, 150}, {0, 90}, {2093, 150}, {0, 90}, {2637, 280}, {0, 420},
  {2093, 150}, {0, 90}, {2093, 150}, {0, 90}, {2637, 280}
};
const size_t TAM_MELODIA_DOSE = sizeof(MELODIA_DOSE) / sizeof(MELODIA_DOSE[0]);

const NotaBuzzer* melodiaAtual = nullptr;
size_t tamMelodiaAtual = 0;
size_t idxNotaAtual = 0;
unsigned long fimNotaMillis = 0;

#if !BUZZER_ATIVO
  #define CANAL_PWM_BUZZER 4
#endif

void buzzerSaida(uint16_t freq) {
#if BUZZER_ATIVO
  digitalWrite(BUZZER_PIN, freq > 0 ? BUZZER_NIVEL_LIGADO : !BUZZER_NIVEL_LIGADO);
#else
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  if (freq > 0) {
    ledcAttach(BUZZER_PIN, freq, 8);
    ledcWriteTone(BUZZER_PIN, freq);
  } else {
    ledcDetach(BUZZER_PIN);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, !BUZZER_NIVEL_LIGADO);
  }
  #else
  if (freq > 0) {
    ledcAttachPin(BUZZER_PIN, CANAL_PWM_BUZZER);
    ledcWriteTone(CANAL_PWM_BUZZER, freq);
  } else {
    ledcWriteTone(CANAL_PWM_BUZZER, 0);
    ledcDetachPin(BUZZER_PIN);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, !BUZZER_NIVEL_LIGADO); // Mantém o módulo desligado em repouso
  }
  #endif
#endif
}

void iniciarBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
#if !BUZZER_ATIVO && !(defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcSetup(CANAL_PWM_BUZZER, 2000, 8);
#endif
  buzzerSaida(0);
  Serial.printf("[BUZZER] Pronto no pino D%d (%s, liga em %s)\n", BUZZER_PIN,
                BUZZER_ATIVO ? "ativo" : "passivo",
                BUZZER_NIVEL_LIGADO == HIGH ? "HIGH" : "LOW");
}

void tocarMelodia(const NotaBuzzer* melodia, size_t tamanho) {
  if (melodia == nullptr || tamanho == 0) return;
  melodiaAtual = melodia;
  tamMelodiaAtual = tamanho;
  idxNotaAtual = 0;
  buzzerSaida(melodia[0].freq);
  fimNotaMillis = millis() + melodia[0].ms;
}

void tocarSinalDeDose() {
  tocarMelodia(MELODIA_DOSE, TAM_MELODIA_DOSE);
}

// Avança as notas sem travar o loop (chamado continuamente no loop)
void atualizarBuzzer() {
  if (melodiaAtual == nullptr) return;
  if ((long)(millis() - fimNotaMillis) < 0) return;

  idxNotaAtual++;
  if (idxNotaAtual >= tamMelodiaAtual) {
    buzzerSaida(0);
    melodiaAtual = nullptr;
    return;
  }
  buzzerSaida(melodiaAtual[idxNotaAtual].freq);
  fimNotaMillis = millis() + melodiaAtual[idxNotaAtual].ms;
}

// ============================================================================
// 10. COMUNICAÇÃO BLUETOOTH COM O APLICATIVO
// ============================================================================
#if !USAR_BLUETOOTH_CLASSICO
// Usa o MTU negociado com o celular para enviar pacotes maiores (menos notificações)
size_t tamanhoPacoteBLE() {
  if (pServer == nullptr) return 20;
  uint16_t mtu = pServer->getPeerMTU(pServer->getConnId());
  if (mtu <= 23) return 20;
  size_t util = mtu - 3;
  return util > 180 ? 180 : util;
}
#endif

void enviarRespostaBT(const String& msg) {
  String linha = msg + "\n";
  Serial.print("[BT TX] " + linha);

#if USAR_BLUETOOTH_CLASSICO
  if (SerialBT.hasClient()) {
    SerialBT.print(linha);
  }
#else
  if (bleConectado && pTxCharacteristic != nullptr) {
    const char* str = linha.c_str();
    size_t len = linha.length();
    const size_t chunkSize = tamanhoPacoteBLE();
    for (size_t i = 0; i < len; i += chunkSize) {
      size_t tam = (len - i < chunkSize) ? (len - i) : chunkSize;
      pTxCharacteristic->setValue((uint8_t*)(str + i), tam);
      pTxCharacteristic->notify();
      delay(6);
    }
  }
#endif
}

void enviarStatusBT() {
  int p = proximoAgendamento();
  String json;
  json.reserve(160 + MAX_AGENDAMENTOS * 90); // Evita realocações/fragmentação de heap
  json += "{\"ocupados\":";
  json += slotsOcupados;
  json += ",\"total\":";
  json += capacidadeTotal;
  json += ",\"rfid\":";
  json += rfidOperacional ? "true" : "false";
  json += ",\"proximo\":";
  json += (p >= 0 ? restanteMs(p) / 1000 : -1);
  json += ",\"agendamentos\":[";

  bool primeiro = true;
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    if (!listaAgendamentos[i].ativo) continue;
    long r = restanteMs(i);
    if (r < 0) r = 0;
    if (!primeiro) json += ",";
    json += "{\"id\":";
    json += i;
    json += ",\"desc\":\"";
    json += listaAgendamentos[i].descricao;
    json += "\",\"restante\":";
    json += r / 1000;
    json += ",\"repete\":";
    json += listaAgendamentos[i].repeteMillis / 1000;
    json += "}";
    primeiro = false;
  }
  json += "]}";
  enviarRespostaBT(json);
}

// ============================================================================
// 11. MECANISMO DE DISPENSAÇÃO MECÂNICA
//     PONTO ÚNICO de liberação: App (DISPENSAR), Tag RFID e Agendamentos
//     passam TODOS por aqui -> o buzzer apita em qualquer liberação.
// ============================================================================
bool acionarDispensador(const String& tituloOLED = "Liberando") {
  if (slotsOcupados > 0 && !emUso) {
    emUso = true;
    slotsOcupados--;
    salvarEstoqueNaFlash();

    updateDisplay(tituloOLED, "Remedio...");
    myStepper.step(PASSOS_POR_DOSE);
    stopMotor(); // Desenergiza as bobinas (evita aquecimento e consumo)

    tocarSinalDeDose(); // Apita SEMPRE que um remédio é liberado (não bloqueia o loop)

    dadosPulseira.slotsRestantes = slotsOcupados;
    notificarPulseira(1);

    showMessage("Dose liberada", String(slotsOcupados) + "/" + String(capacidadeTotal), 2500);
    emUso = false;
    return true;
  } else if (slotsOcupados <= 0) {
    dadosPulseira.slotsRestantes = 0;
    notificarPulseira(2);
    showMessage("ALERTA!", "Caixa Vazia!", 4000);
    return false;
  }
  return false;
}

// ============================================================================
// 12. LEITOR RFID RC522 (COM AUTO-DIAGNÓSTICO E RECONEXÃO AUTOMÁTICA)
// ============================================================================
bool rc522Responde(byte* versaoOut = nullptr) {
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  if (versaoOut) *versaoOut = v;
  return v != 0x00 && v != 0xFF;
}

const char* descreverVersaoRC522(byte v) {
  switch (v) {
    case 0x91: return "MFRC522 v1.0";
    case 0x92: return "MFRC522 v2.0";
    case 0x88: return "Clone FM17522";
    case 0x12: return "Clone compativel";
    default:   return "Chip compativel";
  }
}

bool inicializarLeitorRFID(bool verbose) {
  rfid.PCD_Init(); // Reset por hardware via pino RST + configuração padrão
  delay(10);

  byte versao = 0;
  if (rc522Responde(&versao)) {
    rfid.PCD_SetAntennaGain(MFRC522::RxGain_max); // Ganho máximo = maior alcance
    if (verbose) {
      Serial.printf("[RFID] RC522 OPERACIONAL! %s (0x%02X)\n", descreverVersaoRC522(versao), versao);
    }
    return true;
  }

  if (verbose) {
    Serial.printf("[RFID] ERRO: RC522 nao respondeu (0x%02X).\n", versao);
    Serial.println("       -> Confira o FIO VERDE (MOSI) no D26 e a alimentacao em 3.3V.");
    Serial.println("       -> Nova tentativa automatica a cada 5 segundos.");
  }
  return false;
}

void iniciarRFID() {
  Serial.printf("[RFID] Pinos: SDA=D%d SCK=D%d MOSI=D%d MISO=D%d RST=D%d\n",
                RFID_SDA, RFID_SCK, RFID_MOSI, RFID_MISO, RFID_RST);
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SDA);
  rfidOperacional = inicializarLeitorRFID(true);
  ultimaSaudeRfid = millis();
}

// Monitora a saúde do leitor: detecta mau contato e reconecta sozinho
void manterRFID() {
  if (millis() - ultimaSaudeRfid < INTERVALO_SAUDE_RFID_MS) return;
  ultimaSaudeRfid = millis();

  if (rfidOperacional) {
    if (!rc522Responde()) {
      rfidOperacional = false;
      Serial.println("[RFID] Leitor parou de responder (mau contato?). Tentando reconectar...");
      enviarStatusBT();
    }
    return;
  }

  if (inicializarLeitorRFID(false)) {
    rfidOperacional = true;
    Serial.println("[RFID] Leitor RC522 reconectado com sucesso!");
    showMessage("RFID", "Conectado!", 2000);
    enviarStatusBT();
  }
}

void verificarLeituraRFID() {
  if (!rfidOperacional) return;
  if (millis() - ultimoPollRfid < INTERVALO_POLL_RFID_MS) return;
  ultimoPollRfid = millis();
  if (millis() - ultimoRfidMillis < COOLDOWN_RFID_MS) return;

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  ultimoRfidMillis = millis();

  // Converte o UID para HEX sem alocações repetidas de String
  char uidHex[2 * 10 + 1];
  size_t pos = 0;
  for (byte i = 0; i < rfid.uid.size && i < 10; i++) {
    pos += snprintf(uidHex + pos, sizeof(uidHex) - pos, "%02X", rfid.uid.uidByte[i]);
  }
  uidHex[pos] = '\0';

  Serial.printf("[RFID] Tag de Cuidador/Emergencia lida! UID: %s\n", uidHex);

  // Coloca a tag em HALT: ela só dispara de novo se for afastada e reaproximada
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  String uidStr(uidHex);
  if (slotsOcupados > 0 && !emUso) {
    acionarDispensador("Tag RFID:");
    enviarRespostaBT("{\"evento\":\"DISPENSADO_RFID\",\"origem\":\"RFID\",\"uid\":\"" + uidStr + "\",\"ok\":true}");
  } else {
    showMessage("RFID Cuidador:", "Caixa Vazia!", 3500);
    enviarRespostaBT("{\"evento\":\"VAZIO_RFID\",\"origem\":\"RFID\",\"uid\":\"" + uidStr + "\",\"ok\":false}");
  }
  enviarStatusBT();
}

// ============================================================================
// 13. PROCESSADOR DE COMANDOS (BLUETOOTH E SERIAL USB)
// ============================================================================
void processarComando(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.println("[RX] Comando recebido: " + cmd);

  if (cmd.equalsIgnoreCase("STATUS")) {
    enviarStatusBT();
    return;
  }

  if (cmd.equalsIgnoreCase("DISPENSAR")) {
    if (acionarDispensador("Liberando")) {
      enviarRespostaBT("{\"evento\":\"DISPENSADO\",\"origem\":\"APP\",\"desc\":\"Dispensacao Manual\",\"ok\":true}");
    } else {
      enviarRespostaBT("{\"evento\":\"VAZIO\",\"origem\":\"APP\",\"ok\":false}");
    }
    enviarStatusBT();
    return;
  }

  if (cmd.equalsIgnoreCase("LIMPAR")) {
    for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
      listaAgendamentos[i].ativo = false;
    }
    showMessage("Agenda:", "Limpa!", 2000);
    enviarStatusBT();
    return;
  }

  if (cmd.startsWith("REMOVER:")) {
    int id = cmd.substring(8).toInt();
    if (id >= 0 && id < MAX_AGENDAMENTOS) {
      listaAgendamentos[id].ativo = false;
    }
    enviarStatusBT();
    return;
  }

  if (cmd.startsWith("ESTOQUE:")) {
    int p1 = cmd.indexOf(':');
    int p2 = cmd.indexOf(':', p1 + 1);
    if (p1 > 0 && p2 > p1) {
      int total = cmd.substring(p1 + 1, p2).toInt();
      int ocup = cmd.substring(p2 + 1).toInt();
      if (total < 1) total = 1;
      if (ocup < 0) ocup = 0;
      if (ocup > total) ocup = total;
      capacidadeTotal = total;
      slotsOcupados = ocup;
      salvarEstoqueNaFlash();
      showMessage("Estoque:", String(slotsOcupados) + "/" + String(capacidadeTotal), 2000);
    }
    enviarStatusBT();
    return;
  }

  if (cmd.startsWith("AGENDAR:")) {
    int p1 = cmd.indexOf(':');
    int p2 = cmd.indexOf(':', p1 + 1);
    int p3 = cmd.indexOf(':', p2 + 1);
    if (p1 > 0 && p2 > p1 && p3 > p2) {
      long delaySegundos = cmd.substring(p1 + 1, p2).toInt();
      long repSegundos = cmd.substring(p2 + 1, p3).toInt();
      String descricao = cmd.substring(p3 + 1);

      // Valida valores (evita negativos e estouro do contador millis())
      if (delaySegundos <= 0 || repSegundos < 0 ||
          (unsigned long)delaySegundos > MAX_SEGUNDOS_AGENDA ||
          (unsigned long)repSegundos > MAX_SEGUNDOS_AGENDA) {
        enviarRespostaBT("{\"evento\":\"INVALIDO\",\"ok\":false}");
        return;
      }

      int slotLivre = -1;
      for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
        if (!listaAgendamentos[i].ativo) {
          slotLivre = i;
          break;
        }
      }
      if (slotLivre == -1) {
        enviarRespostaBT("{\"evento\":\"FULL\",\"ok\":false}");
        return;
      }

      descricao.replace("\"", "'");
      descricao.replace("\\", "/");
      if (descricao.length() > 40) descricao = descricao.substring(0, 40);

      listaAgendamentos[slotLivre].alvoMillis = millis() + ((unsigned long)delaySegundos * 1000UL);
      listaAgendamentos[slotLivre].repeteMillis = (unsigned long)repSegundos * 1000UL;
      listaAgendamentos[slotLivre].descricao = descricao;
      listaAgendamentos[slotLivre].ativo = true;

      showMessage("Agendado:", descricao.length() > 10 ? descricao.substring(0, 10) : descricao, 2000);
      enviarRespostaBT("{\"evento\":\"AGENDADO\",\"ok\":true}");
      enviarStatusBT();
      return;
    }
  }

  enviarRespostaBT("{\"evento\":\"COMANDO_DESCONHECIDO\",\"ok\":false}");
}

// ============================================================================
// 14. CALLBACKS E INICIALIZAÇÃO BLE NORDIC UART
// ============================================================================
#if !USAR_BLUETOOTH_CLASSICO
// Envia a linha montada para a fila (executado na task do Bluetooth)
void enfileirarComandoBLE(const char* linha) {
  if (filaComandos == nullptr) return;
  if (xQueueSend(filaComandos, linha, 0) != pdTRUE) {
    Serial.println("[BLE] Fila de comandos cheia, comando descartado.");
  }
}

class CallbacksServidorBLE : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleConectado = true;
    Serial.println("[BLE] Aplicativo conectado com sucesso!");
  }
  void onDisconnect(BLEServer* pServer) override {
    bleConectado = false;
    bufferBLE.len = 0;
    Serial.println("[BLE] Aplicativo desconectado. Reiniciando anuncio...");
    pServer->startAdvertising();
  }
};

class CallbacksRecepcaoBLE : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    auto rxValue = pCharacteristic->getValue();
    for (size_t i = 0; i < rxValue.length(); i++) {
      if (acumularCaractere(bufferBLE, rxValue[i])) {
        enfileirarComandoBLE(bufferBLE.dados);
      }
    }

    // Compatibilidade: comandos curtos enviados sem '\n' (ex: testes no nRF Connect)
    if (bufferBLE.len > 0) {
      bufferBLE.dados[bufferBLE.len] = '\0';
      if (strcasecmp(bufferBLE.dados, "STATUS") == 0 ||
          strcasecmp(bufferBLE.dados, "DISPENSAR") == 0 ||
          strcasecmp(bufferBLE.dados, "LIMPAR") == 0) {
        enfileirarComandoBLE(bufferBLE.dados);
        bufferBLE.len = 0;
      }
    }
  }
};
#endif

void iniciarBluetooth() {
#if USAR_BLUETOOTH_CLASSICO
  SerialBT.begin(NOME_BLUETOOTH);
  Serial.println("[BT] Bluetooth Classico SPP iniciado: " NOME_BLUETOOTH);
#else
  BLEDevice::init(NOME_BLUETOOTH);
  BLEDevice::setMTU(MTU_LOCAL_BLE); // Permite pacotes maiores após negociação com o celular

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new CallbacksServidorBLE());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxCharacteristic->setCallbacks(new CallbacksRecepcaoBLE());

  pService->start();

  // Nome no pacote principal e UUID de 128 bits no Scan Response (limite de 31 bytes)
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setName(NOME_BLUETOOTH);
  pAdvertising->setAdvertisementData(advData);

  BLEAdvertisementData scanRespData;
  scanRespData.setCompleteServices(BLEUUID(SERVICE_UUID));
  pAdvertising->setScanResponseData(scanRespData);

  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Bluetooth Low Energy pronto: " NOME_BLUETOOTH);
#endif
}

// ============================================================================
// 15. ROTINAS DO LOOP
// ============================================================================
void processarEntradasDeComandos() {
#if USAR_BLUETOOTH_CLASSICO
  while (SerialBT.available()) {
    if (acumularCaractere(bufferBT, (char)SerialBT.read())) {
      processarComando(String(bufferBT.dados));
    }
  }
#else
  // Processa todos os comandos BLE pendentes na fila (sem perder comandos seguidos)
  char cmd[TAM_MAX_COMANDO];
  while (filaComandos != nullptr && xQueueReceive(filaComandos, cmd, 0) == pdTRUE) {
    processarComando(String(cmd));
  }

  if (bleConectado && !bleConectadoAnterior) {
    bleConectadoAnterior = true;
    showMessage("Celular", "Conectado!", 2000);
  } else if (!bleConectado && bleConectadoAnterior) {
    bleConectadoAnterior = false;
  }
#endif

  // Comandos pelo Monitor Serial USB (testes no PC)
  while (Serial.available()) {
    if (acumularCaractere(bufferSerial, (char)Serial.read())) {
      processarComando(String(bufferSerial.dados));
    }
  }
}

void processarAgendamentos() {
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    Agendamento& a = listaAgendamentos[i];
    if (!a.ativo || restanteMs(i) > 0) continue;

    String descDisparo = a.descricao;
    if (slotsOcupados > 0) {
      acionarDispensador("Hora da Dose");
      reagendarOuDesativar(a);
      enviarRespostaBT("{\"evento\":\"DISPENSADO\",\"origem\":\"AGENDAMENTO\",\"desc\":\"" + descDisparo + "\",\"ok\":true}");
    } else {
      dadosPulseira.slotsRestantes = 0;
      notificarPulseira(2);
      showMessage("ALERTA!", "Sem Remedio", 4000);
      // Horários recorrentes continuam ativos para a próxima dose após reabastecer
      reagendarOuDesativar(a);
      enviarRespostaBT("{\"evento\":\"VAZIO\",\"origem\":\"AGENDAMENTO\",\"desc\":\"" + descDisparo + "\",\"ok\":false}");
    }
    enviarStatusBT();
  }
}

void atualizarTela() {
  if (msgAtiva && (long)(millis() - msgAte) >= 0) {
    msgAtiva = false;
  }
  if (!msgAtiva && millis() - ultimoRefresh >= INTERVALO_TELA_MS) {
    ultimoRefresh = millis();
    telaPrincipal();
  }
}

// ============================================================================
// 16. SETUP E LOOP PRINCIPAL
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- [E.L.O.S. INICIALIZANDO SISTEMA] ---");

  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    listaAgendamentos[i].ativo = false;
  }

  filaComandos = xQueueCreate(TAM_FILA_COMANDOS, TAM_MAX_COMANDO);

  iniciarBuzzer();
  carregarEstoqueDaFlash();

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  myStepper.setSpeed(VELOCIDADE_MOTOR_RPM);
  stopMotor();

  // Display OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ENDERECO);
  if (oledOk) {
    updateDisplay("E.L.O.S.", "Iniciando...");
  } else {
    Serial.println("[ERRO] Display OLED nao encontrado no I2C (D14/D27).");
  }

  // Comunicação e periféricos
  iniciarBluetooth();
  iniciarRFID();
  iniciarComunicacaoPulseira();

  showMessage("Sistema Pronto", NOME_BLUETOOTH, 2500);
  Serial.printf("[SISTEMA] Pronto. Estoque: %d/%d | RFID: %s\n",
                slotsOcupados, capacidadeTotal, rfidOperacional ? "OK" : "ERRO");
}

void loop() {
  verificarLeituraRFID();       // 1. Tag de Emergência / Cuidador
  manterRFID();                 // 2. Auto-diagnóstico e reconexão do RC522
  processarEntradasDeComandos(); // 3. Comandos do App (BLE) e Serial USB
  processarAgendamentos();      // 4. Disparo automático das doses
  atualizarBuzzer();            // 5. Sinal sonoro de liberação (não-bloqueante)
  atualizarTela();              // 6. Visor OLED
}
