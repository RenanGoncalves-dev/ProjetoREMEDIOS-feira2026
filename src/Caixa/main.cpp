#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Stepper.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// MAC Address EXATO da Pulseira XIAO
uint8_t broadcastAddress[] = {0x84, 0xFC, 0xE6, 0x00, 0xD3, 0x38}; 

// --- Pinos do Motor e Botão ---
#define IN1 5
#define IN2 18
#define IN3 19
#define IN4 21
#define BTN_DISPENSE 4 

// --- Configurações do OLED (Pinos Customizados) ---
#define OLED_SDA 14
#define OLED_SCL 27
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Stepper myStepper(2048, IN1, IN3, IN2, IN4);

typedef struct struct_message {
  int comando;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

void stopMotor() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW); 
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
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

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("[SISTEMA] Pulseira avisada com sucesso!");
    updateDisplay("Pulseira:", "Avisada!");
  } else {
    Serial.println("[ERRO] A pulseira esta desligada ou longe demais.");
    updateDisplay("Erro de Sinal:", "Fora de Area");
  }
  
  delay(2000);
  updateDisplay("Projeto E.L.O.S.", "Aguardando");
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_DISPENSE, INPUT_PULLUP);
  myStepper.setSpeed(12); 

  Wire.begin(OLED_SDA, OLED_SCL);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[ERRO] Falha ao iniciar o display OLED"));
  } else {
    updateDisplay("Sistema NAPNE", "Iniciando...");
    delay(2000);
  }

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    updateDisplay("Erro de Rede", "Falha Wi-Fi");
    return;
  }

  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  
  stopMotor();
  updateDisplay("Projeto E.L.O.S.", "Aguardando");
}

void loop() {
  if (digitalRead(BTN_DISPENSE) == LOW) {
    delay(50); 
    
    if (digitalRead(BTN_DISPENSE) == LOW) {
      Serial.println("[HARDWARE] Botão pressionado! Girando gaveta...");
      updateDisplay("Medicamento", "Liberando..");
      
      myStepper.step(256); 
      stopMotor();
      
      myData.comando = 1;
      esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
      
      while (digitalRead(BTN_DISPENSE) == LOW); 
    }
  }
}
