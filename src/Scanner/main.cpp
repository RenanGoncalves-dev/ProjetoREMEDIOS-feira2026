#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// SCANNER E TESTADOR AUTOMÁTICO DE HARDWARE - E.L.O.S.
// Identifica portas I2C (Display OLED) e testa o Leitor RFID RC522
// ============================================================================

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledAtivo = false;

struct ConfigPinosRFID {
  const char* nome;
  int sda;
  int sck;
  int mosi;
  int miso;
  int rst;
};

// Combinações de portas para testar o RFID RC522
ConfigPinosRFID testesPinos[] = {
  { "Config 1 (Recomendada: D25, D33, D26, D35, D32)", 25, 33, 26, 35, 32 },
  { "Config 2 (Com MOSI no D23)",                       25, 33, 23, 35, 32 },
  { "Config 3 (Antiga com MOSI no D34 - Invalida)",     25, 33, 34, 35, 32 },
  { "Config 4 (Com RST no D26, MOSI no D32)",           25, 33, 32, 35, 26 },
  { "Config 5 (Com SDA no D5, SCK no D18, MOSI no D23)", 5, 18, 23, 19, 22 }
};

const int TOTAL_TESTES = sizeof(testesPinos) / sizeof(testesPinos[0]);

int configAtiva = -1;
MFRC522* rfid = nullptr;

void atualizarTela(String l1, String l2, String l3 = "") {
  if (!oledAtivo) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("=== E.L.O.S. SCANNER ===");
  oled.setCursor(0, 16);
  oled.setTextSize(1);
  oled.println(l1);
  oled.setCursor(0, 30);
  oled.println(l2);
  if (l3.length() > 0) {
    oled.setCursor(0, 46);
    oled.setTextSize(2);
    oled.println(l3);
  }
  oled.display();
}

void escanearI2C() {
  Serial.println("\n--------------------------------------------------");
  Serial.println("[1] ESCANEANDO BARRAMENTO I2C (Display OLED)...");
  Serial.println("    Testando pinos padrao SDA=D14, SCL=D27...");

  Wire.begin(14, 27);
  delay(100);

  int nDispositivos = 0;
  for (byte endereco = 1; endereco < 127; endereco++) {
    Wire.beginTransmission(endereco);
    byte erro = Wire.endTransmission();
    if (erro == 0) {
      Serial.printf("    -> Dispositivo I2C ENCONTRADO no endereco: 0x%02X!\n", endereco);
      nDispositivos++;
    }
  }

  if (nDispositivos == 0) {
    Serial.println("    [AVISO] Nenhum dispositivo I2C achado em D14/D27. Testando D21/D22...");
    Wire.begin(21, 22);
    delay(100);
    for (byte endereco = 1; endereco < 127; endereco++) {
      Wire.beginTransmission(endereco);
      byte erro = Wire.endTransmission();
      if (erro == 0) {
        Serial.printf("    -> Dispositivo I2C ENCONTRADO no endereco 0x%02X (D21/D22)!\n", endereco);
        nDispositivos++;
      }
    }
  }

  if (oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledAtivo = true;
    atualizarTela("I2C OK (0x3C)", "Display Ativo!", "");
    Serial.println("    [OK] Display OLED 128x64 inicializado com sucesso!");
  } else {
    Serial.println("    [AVISO] Display OLED nao respondeu no endereco 0x3C.");
  }
}

byte testarComunicacaoRC522(int sck, int miso, int mosi, int sda, int rst) {
  // Encerra qualquer SPI anterior
  SPI.end();
  delay(20);

  // Inicia pinos SPI
  SPI.begin(sck, miso, mosi, sda);
  pinMode(sda, OUTPUT);
  digitalWrite(sda, HIGH);
  pinMode(rst, OUTPUT);
  digitalWrite(rst, HIGH);

  MFRC522 leitor(sda, rst);
  leitor.PCD_Init();
  delay(25);

  byte versao = leitor.PCD_ReadRegister(MFRC522::VersionReg);
  return versao;
}

void escanearRFID() {
  Serial.println("\n--------------------------------------------------");
  Serial.println("[2] ESCANEANDO LEITOR RFID RC522...");
  Serial.println("    Testando combinacoes de pinos para identificar onde esta ligado:");

  for (int i = 0; i < TOTAL_TESTES; i++) {
    ConfigPinosRFID cfg = testesPinos[i];
    Serial.printf("\n>>> Testando %s\n", cfg.nome);
    Serial.printf("    SDA=D%d | SCK=D%d | MOSI=D%d | MISO=D%d | RST=D%d\n",
                  cfg.sda, cfg.sck, cfg.mosi, cfg.miso, cfg.rst);

    byte v = testarComunicacaoRC522(cfg.sck, cfg.miso, cfg.mosi, cfg.sda, cfg.rst);
    Serial.printf("    Resposta do Chip RC522 (VersionReg): 0x%02X ", v);

    if (v == 0x91) {
      Serial.println("--> SUCESSO! MFRC522 Versao 1.0 Original detectado!");
      configAtiva = i;
      break;
    } else if (v == 0x92) {
      Serial.println("--> SUCESSO! MFRC522 Versao 2.0 Original detectado!");
      configAtiva = i;
      break;
    } else if (v == 0x12) {
      Serial.println("--> SUCESSO! MFRC522 Clone comum funcional detectado!");
      configAtiva = i;
      break;
    } else if (v != 0x00 && v != 0xFF) {
      Serial.printf("--> SUCESSO! Chip RFID compativel detectado (0x%02X)!\n", v);
      configAtiva = i;
      break;
    } else {
      Serial.println("--> Falha (0x00 ou 0xFF: sem resposta eletrica)");
    }
  }

  Serial.println("\n--------------------------------------------------");
  if (configAtiva != -1) {
    ConfigPinosRFID ok = testesPinos[configAtiva];
    Serial.println("✅ SUCESSO TOTAL! O LEITOR RFID ESTA FUNCIONANDO!");
    Serial.printf("   Portas confirmadas que estao funcionando:\n");
    Serial.printf("   -> SDA  (Fio Azul)    : D%d\n", ok.sda);
    Serial.printf("   -> SCK  (Fio Roxo)    : D%d\n", ok.sck);
    Serial.printf("   -> MOSI (Fio Verde)   : D%d\n", ok.mosi);
    Serial.printf("   -> MISO (Fio Amarelo) : D%d\n", ok.miso);
    Serial.printf("   -> RST  (Fio Marrom)  : D%d\n", ok.rst);
    Serial.printf("   -> 3.3V : Pino 3V3\n");
    Serial.printf("   -> GND  : Pino GND\n");

    atualizarTela("RFID CONECTADO!", "Aproxime a tag/cartao", "");

    // Inicializa o leitor definitivo na configuração encontrada
    SPI.begin(ok.sck, ok.miso, ok.mosi, ok.sda);
    rfid = new MFRC522(ok.sda, ok.rst);
    rfid->PCD_Init();
    rfid->PCD_SetAntennaGain(MFRC522::RxGain_max);
  } else {
    Serial.println("❌ NENHUMA PORTA RESPONDEU AO LEITOR RC522 (Retorno sempre 0x00 ou 0xFF)!");
    Serial.println("   Diagnostico das causas fisicas mais comuns:");
    Serial.println("   1. [MUITO COMUM] BARRA DE PINOS NAO SOLDADA:");
    Serial.println("      O modulo RC522 novo vem com a barra de pinos solta.");
    Serial.println("      Se os pinos estao apenas encaixados sem solda de estanho, nao da contato!");
    Serial.println("   2. FIO VERDE (MOSI):");
    Serial.println("      Tem que estar no D26 (GPIO 26). Se estiver no D34, NUNCA vai funcionar.");
    Serial.println("   3. ALIMENTACAO:");
    Serial.println("      O fio 3.3V do RC522 deve estar no pino 3V3 do ESP32.");
    Serial.println("   4. FIOS FROUXOS:");
    Serial.println("      Troque os cabos jumpers dupont (fios partidos por dentro sao muito frequentes).");

    atualizarTela("RFID NAO RESPONDEU", "Verifique fios/solda", "0x00");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println("\n\n==================================================");
  Serial.println("   E.L.O.S. - DIAGNOSTICO E SCANNER DE PORTAS     ");
  Serial.println("==================================================");

  escanearI2C();
  escanearRFID();

  Serial.println("\n[SISTEMA EM MONITORAMENTO CONTÍNUO]");
  if (configAtiva != -1) {
    Serial.println(">> APROXIME O CARTAO OU CHAVEIRO RFID AGORA PARA TESTAR A LEITURA <<");
  } else {
    Serial.println(">> Corrija a fiacao acima e pressione o botao RESET do ESP32 <<");
  }
}

unsigned long ultimoLoopInfo = 0;

void loop() {
  if (configAtiva != -1 && rfid != nullptr) {
    // Verifica se aproximou um cartão ou tag
    if (rfid->PICC_IsNewCardPresent() && rfid->PICC_ReadCardSerial()) {
      String uidStr = "";
      for (byte i = 0; i < rfid->uid.size; i++) {
        if (rfid->uid.uidByte[i] < 0x10) uidStr += "0";
        uidStr += String(rfid->uid.uidByte[i], HEX);
      }
      uidStr.toUpperCase();

      Serial.println("\n🎉🎉 CARTAO / TAG LIDO COM SUCESSO! 🎉🎉");
      Serial.printf("   UID HEX: %s\n", uidStr.c_str());
      Serial.printf("   Tamanho: %d bytes\n", rfid->uid.size);
      Serial.println("----------------------------------------------");

      atualizarTela("CARTAO DETECTADO!", "UID da Tag:", uidStr);

      rfid->PICC_HaltA();
      rfid->PCD_StopCrypto1();
      delay(1500);
      atualizarTela("Aproxime outro", "cartao/tag RFID...", "");
    }
  }

  // A cada 4 segundos imprime uma lembrança se ainda não leu
  if (millis() - ultimoLoopInfo >= 4000) {
    ultimoLoopInfo = millis();
    if (configAtiva != -1) {
      Serial.println("[AGUARDANDO] Passe o cartao RFID sobre o leitor...");
    } else {
      Serial.println("[STATUS] RC522 ainda sem contato eletrico nos pinos. Verifique fiaçao/solda.");
    }
  }
}

