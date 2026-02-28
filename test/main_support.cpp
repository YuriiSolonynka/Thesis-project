#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <LittleFS.h>

#define MSG_DISCOVERY 0
#define MSG_ACCEPTED  1
#define MSG_DATA      2
#define MSG_DELAY     3
#define MSG_RESUME    4

volatile bool sendPermition = false;
volatile bool deliveryFinished = false;
volatile bool deliverySuccess = false;

uint8_t receiverAddress[6];

const char* path = "/myfile.txt";

typedef struct struct_message {
    float coord[30][2];
    uint8_t count;
    uint8_t msgType;
} struct_message;

struct_message incomingData;
struct_message replyData;

void addPairCoordinate (float x, float y, File &file) {
  char string[25];
  sprintf(string, "%.6f %.6f\n", x, y);
  file.write((const uint8_t*)string, strlen(string));
}

void create_data() {
  File file = LittleFS.open(path, "a");
  for (int i = 0; i < 85; i++) {
    addPairCoordinate(i/0.33, i/0.73, file);
  }
  file.close();
  Serial.println("Data created");
}

std::vector<std::pair<float, float>> getAllCoordinate() {
  std::vector<std::pair<float, float>> coordinate_vec;
  float x, y;
  File file = LittleFS.open(path, "r");
  while (file.available()) {
    String iter_str = file.readStringUntil('\n');
    iter_str.trim();  //
    if (iter_str.length() == 0) continue;  //
    sscanf(iter_str.c_str(), "%f %f", &x, &y);
    std::pair<float, float> iter_pair = {x, y};
    coordinate_vec.push_back(iter_pair);
  }
  file.close();
  file = LittleFS.open(path, "w");
  file.close();
  return coordinate_vec;
}

void cutSendData() {
  std::vector<std::pair<float, float>> coord = getAllCoordinate();
  Serial.println("Data read complete");
  while (!coord.empty() && sendPermition) {
    unsigned char packetSize = (coord.size() >= 30) ? 30 : coord.size();
    memset(&replyData, 0, sizeof(replyData));
    replyData.count = packetSize;
    replyData.msgType = MSG_DATA;
    for (int i = 0; i < packetSize; i++) {
      replyData.coord[i][0] = coord[i].first;
      replyData.coord[i][1] = coord[i].second;
    }

    deliveryFinished = false;
    deliverySuccess = false;

    esp_err_t result = esp_now_send(receiverAddress, (uint8_t *) &replyData, sizeof(replyData));

    if (result != ESP_OK) {
      Serial.println("Send error"); 
      break;
    }
    unsigned long startWait = millis();
    while (!deliveryFinished && (millis() - startWait < 100)) {
      yield();
    }

    if (deliverySuccess) {
      coord.erase(coord.begin(), coord.begin() + packetSize);
      Serial.println("Erased data");
    } else {
      Serial.println("Not respond");
      break;
    }
    delay(20);
  }
  
  if (!coord.empty()) {
    File file = LittleFS.open(path, "a");
    if (file) {
      for (const auto& p : coord) {
        addPairCoordinate(p.first, p.second, file);
      }
      file.close();
    }
    Serial.println("Залишки записано назад у файл.");
    } else {
    Serial.println("All data sent successfully!");
    sendPermition = false; 
    }
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataPtr, int len) {
  memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));

  // Якщо це запит на пошук
  if (incomingData.msgType == MSG_DISCOVERY) {

    // Перевіряємо, чи цей пристрій вже є в нашому списку пірів
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, mac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      
      
      if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        Serial.println("Нового піра додано успішно.");
      }
    }
    sendPermition = true;
    memcpy(receiverAddress, mac, 6);
    // Відправляємо підтвердження назад
    replyData.msgType = MSG_ACCEPTED;
    esp_now_send(mac, (uint8_t *) &replyData, sizeof(replyData));
  }
  if(incomingData.msgType == MSG_DELAY) sendPermition = false;
  if(incomingData.msgType == MSG_RESUME) sendPermition = true;
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  deliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
  deliveryFinished = true;
}

void setup() {
  Serial.begin(115200);
  delay(2000); //
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Init error ESP-NOW");
    return;
  }
  if (LittleFS.begin()) Serial.println("++");
  create_data();

  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);
}

void loop() {
  if(sendPermition){
    cutSendData();
  }
}