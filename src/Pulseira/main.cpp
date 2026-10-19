#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// --- Credenciais da Rede gerada pela Caixa ---
const char* ssid = "GREENLANTERN 2532";
const char* password = "verde123";

#define BUZZER_PIN D5

typedef struct struct_message { 
  int comando; 
} struct_message;

struct_message myData;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  
  if (myData.comando == 1) {
    Serial.println("[AVISO] Comando recebido! O remedio foi dispensado.");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(2000); 
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("\n[PULSEIRA] Iniciando...");
  
  // 1. Conecta na rede da Caixa para alinhar o canal do rádio
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("[PULSEIRA] Conectando no Wi-Fi GREENLANTERN 2532...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[PULSEIRA] Conectado e Sincronizado!");

  // 2. Inicia o rádio ESP-NOW
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
    Serial.println("[PULSEIRA] ESP-NOW OK! Monitorando a caixa...");
  } else {
    Serial.println("[ERRO] Falha no ESP-NOW.");
  }

Serial.println("\n[PULSEIRA] Conectado e Sincronizado!");
  
  // ADICIONE ESTAS DUAS LINHAS:
  Serial.print("[PULSEIRA] MEU MAC ADDRESS É: ");
  Serial.println(WiFi.macAddress());
}

void loop() { 
  // Fica aguardando o aviso em tempo real
}