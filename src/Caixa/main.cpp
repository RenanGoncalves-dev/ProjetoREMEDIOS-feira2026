#include <Arduino.h>
#include <Stepper.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// ============================================================================
// CONFIGURAÇÕES DE MODO (BLUETOOTH E PULSEIRA)
// ============================================================================
// 0 = Bluetooth Low Energy (BLE UART) -> Ideal para App HTML/APK moderno, Flutter, Web Bluetooth
// 1 = Bluetooth Clássico (SerialBT SPP) -> Ideal se for criar APK no MIT App Inventor / Kodular padrão
#define USAR_BLUETOOTH_CLASSICO 0

// 0 = Pulseira desativada (pronta para quando você modelar em 3D e programar)
// 1 = Ativa o envio de alertas para a Pulseira via ESP-NOW (sem precisar de roteador!)
#define ATIVAR_PULSEIRA 0

#define NOME_BLUETOOTH "ELOS_Caixa"

#if USAR_BLUETOOTH_CLASSICO
  #include <BluetoothSerial.h>
  BluetoothSerial SerialBT;
#else
  #include <BLEDevice.h>
  #include <BLEServer.h>
  #include <BLEUtils.h>
  #include <BLE2902.h>

  // UUIDs padrão do Nordic UART Service (compatível com qualquer App BLE e Web Bluetooth)
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
// HARDWARE: MOTOR DE PASSO E DISPLAY OLED
// ============================================================================
#define IN1 5
#define IN2 18
#define IN3 19
#define IN4 21

#define OLED_SDA 14
#define OLED_SCL 27

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Stepper myStepper(2048, IN1, IN3, IN2, IN4);
Preferences prefs;

// ============================================================================
// ESTRUTURA PRONTA PARA A PULSEIRA (FUTURA IMPLEMENTAÇÃO)
// ============================================================================
uint8_t macPulseira[] = {0x84, 0xFC, 0xE6, 0x00, 0xD3, 0x38};

// Códigos de evento que a Caixa poderá mandar para a Pulseira:
// 1 = Dose dispensada (tomar remédio!)
// 2 = Alerta de Caixa Vazia (precisa reabastecer)
typedef struct struct_message {
  int comando;
  int slotsRestantes;
} struct_message;

struct_message dadosPulseira;

void iniciarComunicacaoPulseira() {
#if ATIVAR_PULSEIRA
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Garante operação offline sem depender de roteador
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macPulseira, 6);
    peerInfo.channel = 1; // Canal fixo offline
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("[PULSEIRA] Interface ESP-NOW pronta.");
  }
#endif
}

void notificarPulseira(int codigoComando) {
  dadosPulseira.comando = codigoComando;
#if ATIVAR_PULSEIRA
  esp_now_send(macPulseira, (uint8_t*)&dadosPulseira, sizeof(dadosPulseira));
  Serial.printf("[PULSEIRA] Evento %d enviado para a pulseira.\n", codigoComando);
#else
  // Gancho pronto: quando programar a pulseira, mude ATIVAR_PULSEIRA para 1
  Serial.printf("[PULSEIRA - STANDBY] Evento %d gerado (aguardando integracao futura).\n", codigoComando);
#endif
}

// ============================================================================
// ESTADO DA CAIXA: ESTOQUE E AGENDAMENTOS
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

// Controle do OLED
bool msgAtiva = false;
unsigned long msgAte = 0;
unsigned long ultimoRefresh = 0;

// Buffer Serial USB (permite testar os mesmos comandos do Bluetooth pelo PC)
String bufferSerial = "";

// ============================================================================
// PERSISTÊNCIA NA MEMÓRIA FLASH (NVS)
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
// UTILIDADES E DISPLAY OLED
// ============================================================================
void stopMotor() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
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
    if (idx == -1 || r < menor) { idx = i; menor = r; }
  }
  return idx;
}

void formatarTempo(long ms, char* buf, size_t tam) {
  if (ms < 0) ms = 0;
  long s = ms / 1000;
  long h = s / 3600, m = (s % 3600) / 60, x = s % 60;
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

void showMessage(String l1, String l2, unsigned long ms = 2500) {
  updateDisplay(l1, l2);
  msgAtiva = true;
  msgAte = millis() + ms;
}

void telaPrincipal() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Linha superior: Status do Bluetooth
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (bluetoothEstaConectado()) {
    display.print("BT: Conectado [APP]");
  } else {
    display.print("BT: ");
    display.print(NOME_BLUETOOTH);
  }

  // Estoque
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.print("Est:");
  display.print(slotsOcupados);
  display.print("/");
  display.print(capacidadeTotal);

  // Próximo agendamento
  display.setTextSize(1);
  display.setCursor(0, 36);

  int p = proximoAgendamento();
  if (p < 0) {
    display.print("Sem agendamento");
    display.setTextSize(2);
    display.setCursor(0, 47);
    display.print("--:--:--");
  } else {
    char buf[12];
    formatarTempo(restanteMs(p), buf, sizeof(buf));
    display.print("Proxima dose em:");
    display.setTextSize(2);
    display.setCursor(0, 47);
    display.print(buf);
  }
  display.display();
}

// ============================================================================
// COMUNICAÇÃO BLUETOOTH (ENVIO E RECEBIMENTO)
// ============================================================================

// Envia uma linha (JSON ou resposta) para o celular via Bluetooth e para a Serial USB
void enviarRespostaBT(const String& msg) {
  String linha = msg + "\n";
  Serial.print("[BT TX] " + linha);

#if USAR_BLUETOOTH_CLASSICO
  if (SerialBT.hasClient()) {
    SerialBT.print(linha);
  }
#else
  if (bleConectado && pTxCharacteristic != nullptr) {
    // Divide em pacotes de até 20 bytes para compatibilidade total com qualquer celular/MTU
    const char* str = linha.c_str();
    size_t len = linha.length();
    const size_t chunkSize = 20;
    for (size_t i = 0; i < len; i += chunkSize) {
      size_t tam = (len - i < chunkSize) ? (len - i) : chunkSize;
      pTxCharacteristic->setValue((uint8_t*)(str + i), tam);
      pTxCharacteristic->notify();
      delay(8); // Pequena pausa para a pilha BLE entregar todos os pacotes em ordem
    }
  }
#endif
}

// Monta e envia o JSON de status completo para o Aplicativo
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
// DISPENSADOR
// ============================================================================
void acionarDispensador() {
  if (slotsOcupados > 0 && !emUso) {
    emUso = true;
    slotsOcupados--;
    salvarEstoqueNaFlash();

    updateDisplay("Liberando", "Remedio...");
    myStepper.step(276); // 45 graus (1/8 de volta)
    stopMotor();

    // Notifica a pulseira (gancho pronto para implementação futura)
    dadosPulseira.slotsRestantes = slotsOcupados;
    notificarPulseira(1);

    showMessage("Dose liberada", String(slotsOcupados) + "/" + String(capacidadeTotal), 2500);
    emUso = false;
  } else if (slotsOcupados <= 0) {
    dadosPulseira.slotsRestantes = 0;
    notificarPulseira(2);
    showMessage("ALERTA!", "Caixa Vazia!", 4000);
  }
}

// ============================================================================
// PROCESSADOR DE COMANDOS BLUETOOTH / SERIAL
// Protocolo aceito pelo App (.apk):
//   STATUS
//   DISPENSAR
//   ESTOQUE:<total>:<ocupados>          Ex: ESTOQUE:7:5
//   AGENDAR:<delay>:<repetir>:<desc>    Ex: AGENDAR:28800:28800:A cada 8h
//   REMOVER:<id>                        Ex: REMOVER:0
//   LIMPAR
// ============================================================================
void processarComando(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.println("[BT RX] Comando recebido: " + cmd);

  if (cmd.equalsIgnoreCase("STATUS")) {
    enviarStatusBT();
    return;
  }

  if (cmd.equalsIgnoreCase("DISPENSAR")) {
    if (slotsOcupados > 0 && !emUso) {
      acionarDispensador();
      enviarRespostaBT("{\"evento\":\"DISPENSADO\",\"ok\":true}");
    } else {
      showMessage("ALERTA!", "Caixa Vazia!", 4000);
      enviarRespostaBT("{\"evento\":\"VAZIO\",\"ok\":false}");
    }
    enviarStatusBT();
    return;
  }

  if (cmd.equalsIgnoreCase("LIMPAR")) {
    for (int i = 0; i < MAX_AGENDAMENTOS; i++) listaAgendamentos[i].ativo = false;
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
    // Formato: ESTOQUE:<total>:<ocupados>
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
    // Formato: AGENDAR:<delaySegundos>:<repetirSegundos>:<descricao>
    int p1 = cmd.indexOf(':');
    int p2 = cmd.indexOf(':', p1 + 1);
    int p3 = cmd.indexOf(':', p2 + 1);
    if (p1 > 0 && p2 > p1 && p3 > p2) {
      int slotLivre = -1;
      for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
        if (!listaAgendamentos[i].ativo) { slotLivre = i; break; }
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
// CALLBACKS DO BLUETOOTH LOW ENERGY (BLE)
// ============================================================================
#if !USAR_BLUETOOTH_CLASSICO
class CallbacksServidorBLE : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleConectado = true;
    Serial.println("[BLE] Celular conectado!");
  }
  void onDisconnect(BLEServer* pServer) override {
    bleConectado = false;
    Serial.println("[BLE] Celular desconectado. Reiniciando anuncio...");
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
      // Caso o app envie sem '\n' no final, agenda processamento se terminar comando completo
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
  Serial.println("[BT] Bluetooth Classico SPP iniciado como: " NOME_BLUETOOTH);
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
  // Pacote principal (< 31 bytes): garante que o nome "ELOS_Caixa" sempre apareça na busca do celular
  BLEAdvertisementData advData;
  advData.setFlags(0x06); // General Discoverable + BR/EDR Not Supported
  advData.setName(NOME_BLUETOOTH);
  pAdvertising->setAdvertisementData(advData);

  // Scan Response (< 31 bytes): envia o UUID de 128 bits separadamente sem cortar o nome
  BLEAdvertisementData scanRespData;
  scanRespData.setCompleteServices(BLEUUID(SERVICE_UUID));
  pAdvertising->setScanResponseData(scanRespData);

  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Bluetooth Low Energy iniciado como: " NOME_BLUETOOTH);
#endif
}

// ============================================================================
// SETUP E LOOP PRINCIPAL
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int i = 0; i < MAX_AGENDAMENTOS; i++) listaAgendamentos[i].ativo = false;

  carregarEstoqueDaFlash();
  myStepper.setSpeed(12);
  stopMotor();

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERRO] Falha ao iniciar display OLED");
  }

  updateDisplay("Iniciando", "Bluetooth...");
  iniciarBluetooth();
  iniciarComunicacaoPulseira();

  showMessage("Bluetooth OK:", NOME_BLUETOOTH, 3500);
  Serial.println("[SISTEMA] Caixa inteligente pronta (100% Offline via Bluetooth)!");
}

void loop() {
  // 1. Leitura de comandos vindos do Bluetooth
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

  // Envia o status automaticamente assim que o celular conecta
  if (bleConectado && !bleConectadoAnterior) {
    bleConectadoAnterior = true;
    showMessage("Celular", "Conectado!", 2000);
  } else if (!bleConectado && bleConectadoAnterior) {
    bleConectadoAnterior = false;
  }
#endif

  // 2. Também aceita os mesmos comandos via cabo USB (Monitor Serial) para facilitar testes!
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

  // 3. Disparo automático dos agendamentos
  for (int i = 0; i < MAX_AGENDAMENTOS; i++) {
    if (listaAgendamentos[i].ativo && restanteMs(i) <= 0) {
      if (slotsOcupados > 0) {
        acionarDispensador();
        if (listaAgendamentos[i].repeteMillis > 0) {
          listaAgendamentos[i].alvoMillis = millis() + listaAgendamentos[i].repeteMillis;
        } else {
          listaAgendamentos[i].ativo = false;
        }
        enviarStatusBT();
      } else {
        dadosPulseira.slotsRestantes = 0;
        notificarPulseira(2);
        showMessage("ALERTA!", "Sem Remedio", 4000);
        listaAgendamentos[i].ativo = false;
        enviarStatusBT();
      }
    }
  }

  // 4. Atualização do visor OLED (1x por segundo)
  if (msgAtiva && (long)(millis() - msgAte) >= 0) msgAtiva = false;

  if (!msgAtiva && millis() - ultimoRefresh >= 1000) {
    ultimoRefresh = millis();
    telaPrincipal();
  }
}
