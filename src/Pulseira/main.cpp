#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// Pino do Buzzer (ou vibracall/LED) na Pulseira
#define BUZZER_PIN D5

typedef struct struct_message {
  int comando;
} struct_message;

struct_message myData;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  
  if (myData.comando == 1) {
    Serial.println("[AVISO] O remédio foi dispensado na caixa!");
    
    // Toca o alarme da pulseira por 2 segundos
    digitalWrite(BUZZER_PIN, HIGH);
    delay(2000); 
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.mode(WIFI_STA);
  delay(500); 
  
  Serial.print("[SISTEMA] MAC ADDRESS DESTA PULSEIRA (XIAO): ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }
  
  // Cast explícito para evitar erros de compilação
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  
  Serial.println("[SISTEMA] Pulseira pronta! Monitorando a caixa...");
}

void loop() {
  // A pulseira não faz nada no loop, apenas escuta o ESP-NOW
}
