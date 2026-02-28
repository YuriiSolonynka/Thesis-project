#include <esp_now.h>
#include <WiFi.h>

#define MSG_DISCOVERY 0
#define MSG_ACCEPTED  1
#define MSG_DATA      2

typedef struct struct_message {
  uint8_t msgType;
  int sensorValue;
} struct_message;

struct_message sendData;
struct_message incomingData;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t hubAddress[6];

bool isPaired = false;
unsigned long lastDiscoveryTime = 0;
unsigned long lastDataTime = 0;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataPtr, int len) {
  memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));

  if (!isPaired && incomingData.msgType == MSG_ACCEPTED) {
    Serial.println("Отримано відповідь від Хаба! Парування успішне.");
    
    memcpy(hubAddress, mac, 6);
    isPaired = true;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, hubAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (!esp_now_is_peer_exist(hubAddress)) {
      esp_now_add_peer(&peerInfo);
    }
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Init error ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop() {
  unsigned long currentMillis = millis();

  // Якщо ми ще не знайшли Хаб, кожні 2 секунди надсилаємо широкомовний запит
  if (!isPaired) {
    if (currentMillis - lastDiscoveryTime > 2000) {
      Serial.println("Шукаю Хаб (відправка Broadcast)...");
      sendData.msgType = MSG_DISCOVERY;
      sendData.sensorValue = 0;
      esp_now_send(broadcastAddress, (uint8_t *) &sendData, sizeof(sendData));
      lastDiscoveryTime = currentMillis;
    }
  } 
  // Якщо ми вже в парі, надсилаємо корисні дані кожні 3 секунди
  else {
    if (currentMillis - lastDataTime > 3000) {
      sendData.msgType = MSG_DATA;
      sendData.sensorValue = random(10, 50); // Генеруємо якісь дані
      
      Serial.print("Відправка даних на Хаб: ");
      Serial.println(sendData.sensorValue);
      
      // Відправляємо ТІЛЬКИ на збережену адресу Хаба
      esp_now_send(hubAddress, (uint8_t *) &sendData, sizeof(sendData));
      lastDataTime = currentMillis;
    }
  }
}