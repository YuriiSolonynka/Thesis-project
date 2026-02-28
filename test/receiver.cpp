#include <esp_now.h>
#include <WiFi.h>
#include <vector>

typedef struct struct_message {
    float coord[30][2];
    uint8_t count;
    uint8_t msgType
} struct_message;

struct_message myData;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.println(myData.count);
  for (int i = 0; i < myData.count; i++){
    Serial.print("x: ");
    Serial.println(myData.coord[i][0]);
    Serial.print("y: ");
    Serial.println(myData.coord[i][1]);
  }
}
 
void setup() {
  Serial.begin(115200);
  
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}
 
void loop() {
}