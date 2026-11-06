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
// ============================================================================
// MOTOR DE PASSO 28BYJ-48 + DRIVER ULN2003
#define IN1 5   // GPIO 5  -> Driver IN1
#define IN2 18  // GPIO 18 -> Driver IN2
#define IN3 19  // GPIO 19 -> Driver IN3
#define IN4 21  // GPIO 21 -> Driver IN4

// DISPLAY OLED 0.96" I2C (SSD1306 128x64)
#define OLED_SDA 14 // GPIO 14 -> Display SDA
#define OLED_SCL 27 // GPIO 27 -> Display SCL

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

// ============================================================================
// 3. INSTÂNCIAS DOS DISPOSITIVOS
// ============================================================================
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Stepper myStepper(2048, IN1, IN3, IN2, IN4);
MFRC522 rfid(RFID_SDA, RFID_RST);
Preferences prefs;

#if USAR_BLUETOOTH_CLASSICO
  #include <BluetoothSerial.h>
  BluetoothSerial SerialBT;
#else
  #include <BLEDevice.h>
  #include <BLEServer.h>
  #include <BLEUtils.h>
  #include <BLE2902.h>

  #define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
  #define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Celular -> Caixa
  #define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // Caixa -> Celular

  BLEServer* pServer = nullptr;
  BLECharacteristic* pTxCharacteristic = nullptr;
  bool bleConectado = false;
  bool bleConectadoAnterior = false;
  String filaComandoBLE = "";
  volatile bool comandoBLERecebido = false;
#endif

#if ATIVAR_PULSEIRA
  #include <WiFi.h>
  #include <esp_now.h>
#endif

// ============================================================================
// 4. ESTRUTURA PARA A PULSEIRA (EXPANSÃO FUTURA)
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
// 5. ESTADO DA CAIXA: ESTOQUE, AGENDAMENTOS E COOLDOWNS
// ============================================================================
int capacidadeTotal = 7;
int slotsOcupados = 0;
bool emUso = false;

#define MAX_AGENDAMENTOS 8
struct Agendamento {
  unsigned long alvoMillis;
  unsigned long repeteMillis;
  bool ativo;
  String descricao;
};
Agendamento listaAgendamentos[MAX_AGENDAMENTOS];

// Controle de mensagens do OLED
bool msgAtiva = false;
unsigned long msgAte = 0;
unsigned long ultimoRefresh = 0;

// Leitor RFID
bool rfidOperacional = false;
unsigned long ultimoRfidMillis = 0;
const unsigned long COOLDOWN_RFID_MS = 2500; // Evita múltiplas leituras no mesmo toque do cartão

// Buffer Serial USB
String bufferSerial = "";

// Protótipos das funções auxiliares
void enviarStatusBT();
void enviarRespostaBT(const String& msg);
void updateDisplay(String linha1, String linha2);
void showMessage(String l1, String l2, unsigned long ms = 2500);

// ============================================================================
// 6. PERSISTÊNCIA NA FLASH (NVS / PREFERENCES)
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
// 7. CONTROLE DO MOTOR E DISPLAY
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

void updateDisplay(String linha1, String linha2) {
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

void showMessage(String l1, String l2, unsigned long ms) {
  updateDisplay(l1, l2);
  msgAtiva = true;
  msgAte = millis() + ms;
}

void telaPrincipal() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Linha superior: Status Bluetooth + Status RFID
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (bluetoothEstaConectado()) {
    display.print("BT:OK ");
  } else {
    display.print("BT:OFF ");
  }
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
  if (p < 0) {
    display.print("Sem agendamento");
    display.setTextSize(2);
    display.setCursor(0, 48);
    display.print("--:--:--");
  } else {
    char buf[12];
    formatarTempo(restanteMs(p), buf, sizeof(buf));
    display.print("Proxima dose em:");
    display.setTextSize(2);
    display.setCursor(0, 48);
    display.print(buf);
  }
  display.display();
}

// ============================================================================
// 8. COMUNICAÇÃO BLUETOOTH COM O APLICATIVO
// ============================================================================
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
    const size_t chunkSize = 20;
    for (size_t i = 0; i < len; i += chunkSize) {
      size_t tam = (len - i < chunkSize) ? (len - i) : chunkSize;
      pTxCharacteristic->setValue((uint8_t*)(str + i), tam);
      pTxCharacteristic->notify();
      delay(8);
    }
  }
#endif
}

void enviarStatusBT() {
  int p = proximoAgendamento();
  String json = "{\"ocupados\":" + String(slotsOcupados) +
                ",\"total\":" + String(capacidadeTotal) +
                ",\"proximo\":" + String(p >= 0 ? restanteMs(p) / 1000 : -1) +
                ",\"agendamentos\":[";
  bool primeiro = true;
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    if (!listaAgendamentos[i].ativo) continue;
    long r = restanteMs(i);
    if (r < 0) r = 0;
    if (!primeiro) json += ",";
    json += "{\"id\":" + String(i) +
            ",\"desc\":\"" + listaAgendamentos[i].descricao + "\"" +
            ",\"restante\":" + String(r / 1000) +
            ",\"repete\":" + String(listaAgendamentos[i].repeteMillis / 1000) + "}";
    primeiro = false;
  }
  json += "]}";
  enviarRespostaBT(json);
}

// ============================================================================
// 9. MECANISMO DE DISPENSAÇÃO MECÂNICA
// ============================================================================
bool acionarDispensador(const String& tituloOLED = "Liberando") {
  if (slotsOcupados > 0 && !emUso) {
    emUso = true;
    slotsOcupados--;
    salvarEstoqueNaFlash();

    updateDisplay(tituloOLED, "Remedio...");
    myStepper.step(276); // Gira 45 graus (1/8 de volta)
    stopMotor();

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
// 10. LEITOR RFID RC522 (REFATORADO E ROBUSTO)
// ============================================================================
void iniciarRFID() {
  Serial.println("[RFID] Inicializando RC522...");
  Serial.printf("       Pinos: SCK=D%d, MISO=D%d, MOSI=D%d, SDA=D%d, RST=D%d\n",
                RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SDA, RFID_RST);

  // Inicia o barramento SPI nos pinos dedicados
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SDA);
  rfid.PCD_Init();
  delay(35);

  byte versao = rfid.PCD_ReadRegister(MFRC522::VersionReg);

  if (versao != 0x00 && versao != 0xFF) {
    rfidOperacional = true;
    rfid.PCD_SetAntennaGain(MFRC522::RxGain_max); // Força ganho máximo da antena
    Serial.printf("[RFID] RC522 OPERACIONAL! Versao do chip: 0x%02X\n", versao);
  } else {
    rfidOperacional = false;
    Serial.println("[RFID] ERRO: Leitor RC522 nao respondeu (0x00/0xFF).");
    Serial.println("       -> Verifique se o FIO VERDE (MOSI) esta no D26!");
    Serial.println("       -> Verifique a alimentacao no 3.3V.");
  }
}

void verificarLeituraRFID() {
  if (!rfidOperacional) return;
  if (millis() - ultimoRfidMillis < COOLDOWN_RFID_MS) return;

  // Verifica se há cartão presente e faz a leitura
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  ultimoRfidMillis = millis();

  // Converte o UID para string HEX
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  Serial.println("[RFID] Tag de Cuidador/Emergencia lida com sucesso! UID: " + uidStr);

  // Finaliza a comunicação com a tag atual
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

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
// 11. PROCESSADOR DE COMANDOS (BLUETOOTH E SERIAL USB)
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

      unsigned long delaySegundos = cmd.substring(p1 + 1, p2).toInt();
      unsigned long repSegundos = cmd.substring(p2 + 1, p3).toInt();
      String descricao = cmd.substring(p3 + 1);

      if (delaySegundos == 0) {
        enviarRespostaBT("{\"evento\":\"INVALIDO\",\"ok\":false}");
        return;
      }

      descricao.replace("\"", "'");
      descricao.replace("\\", "/");
      if (descricao.length() > 40) descricao = descricao.substring(0, 40);

      listaAgendamentos[slotLivre].alvoMillis = millis() + (delaySegundos * 1000UL);
      listaAgendamentos[slotLivre].repeteMillis = repSegundos * 1000UL;
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
// 12. CALLBACKS E INICIALIZAÇÃO BLE NORDIC UART
// ============================================================================
#if !USAR_BLUETOOTH_CLASSICO
class CallbacksServidorBLE : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleConectado = true;
    Serial.println("[BLE] Aplicativo conectado com sucesso!");
  }
  void onDisconnect(BLEServer* pServer) override {
    bleConectado = false;
    Serial.println("[BLE] Aplicativo desconectado. Reiniciando anuncio...");
    pServer->startAdvertising();
  }
};

class CallbacksRecepcaoBLE : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      for (size_t i = 0; i < rxValue.length(); i++) {
        char c = rxValue[i];
        if (c == '\n' || c == '\r') {
          if (filaComandoBLE.length() > 0) {
            comandoBLERecebido = true;
          }
        } else {
          filaComandoBLE += c;
        }
      }
      if (!comandoBLERecebido && filaComandoBLE.length() > 0 &&
          (filaComandoBLE.equalsIgnoreCase("STATUS") ||
           filaComandoBLE.equalsIgnoreCase("DISPENSAR") ||
           filaComandoBLE.equalsIgnoreCase("LIMPAR"))) {
        comandoBLERecebido = true;
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
// 13. SETUP E LOOP PRINCIPAL
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n--- [E.L.O.S. INICIALIZANDO SISTEMA] ---");

  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    listaAgendamentos[i].ativo = false;
  }

  carregarEstoqueDaFlash();
  myStepper.setSpeed(12);
  stopMotor();

  // Inicializa Display OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERRO] Display OLED nao encontrado no I2C (D14/D27).");
  } else {
    updateDisplay("E.L.O.S.", "Iniciando...");
  }

  // Inicializa Comunicação e Periféricos
  iniciarBluetooth();
  iniciarRFID();
  iniciarComunicacaoPulseira();

  showMessage("Sistema Pronto", NOME_BLUETOOTH, 2500);
  Serial.println("[SISTEMA] Inicializacao completa. Aguardando comandos ou tags.");
}

void loop() {
  // 1. Monitora aproximação de Cartão RFID de Emergência / Cuidador
  verificarLeituraRFID();

  // 2. Processa comandos vindos do Bluetooth
#if USAR_BLUETOOTH_CLASSICO
  while (SerialBT.available()) {
    char c = (char)SerialBT.read();
    if (c == '\n' || c == '\r') {
      if (bufferSerial.length() > 0) {
        processarComando(bufferSerial);
        bufferSerial = "";
      }
    } else {
      bufferSerial += c;
    }
  }
#else
  if (comandoBLERecebido) {
    String cmd = filaComandoBLE;
    filaComandoBLE = "";
    comandoBLERecebido = false;
    processarComando(cmd);
  }

  if (bleConectado && !bleConectadoAnterior) {
    bleConectadoAnterior = true;
    showMessage("Celular", "Conectado!", 2000);
  } else if (!bleConectado && bleConectadoAnterior) {
    bleConectadoAnterior = false;
  }
#endif

  // 3. Processa comandos pelo Monitor Serial USB (para testes no PC)
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (bufferSerial.length() > 0) {
        processarComando(bufferSerial);
        bufferSerial = "";
      }
    } else {
      bufferSerial += c;
    }
  }

  // 4. Disparo automático dos agendamentos programados
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    if (listaAgendamentos[i].ativo && restanteMs(i) <= 0) {
      String descDisparo = listaAgendamentos[i].descricao;
      if (slotsOcupados > 0) {
        acionarDispensador("Hora da Dose");
        if (listaAgendamentos[i].repeteMillis > 0) {
          listaAgendamentos[i].alvoMillis = millis() + listaAgendamentos[i].repeteMillis;
        } else {
          listaAgendamentos[i].ativo = false;
        }
        enviarRespostaBT("{\"evento\":\"DISPENSADO\",\"origem\":\"AGENDAMENTO\",\"desc\":\"" + descDisparo + "\",\"ok\":true}");
        enviarStatusBT();
      } else {
        dadosPulseira.slotsRestantes = 0;
        notificarPulseira(2);
        showMessage("ALERTA!", "Sem Remedio", 4000);
        listaAgendamentos[i].ativo = false;
        enviarRespostaBT("{\"evento\":\"VAZIO\",\"origem\":\"AGENDAMENTO\",\"desc\":\"" + descDisparo + "\",\"ok\":false}");
        enviarStatusBT();
      }
    }
  }

  // 5. Atualização periódica da tela OLED (1x por segundo)
  if (msgAtiva && (long)(millis() - msgAte) >= 0) {
    msgAtiva = false;
  }

  if (!msgAtiva && millis() - ultimoRefresh >= 1000) {
    ultimoRefresh = millis();
    telaPrincipal();
  }
}
