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
    float coord[20][2];
    uint32_t time[20];
    uint8_t count;
    uint8_t msgType;
} struct_message;

struct_message incomingData;
struct_message replyData;

typedef struct GPS_data
{
  float x;
  float y;
  uint32_t time;
};


//дописати розкладання часу (?)
void addPairCoordinate (float x, float y, uint32_t time, File &file) {
  char string[50];
  sprintf(string, "%.6f %.6f %u\n", x, y, time);
  file.write((const uint8_t*)string, strlen(string));
}

//заглушка
void create_data() {
  File file = LittleFS.open(path, "a");
  for (int i = 0; i < 85; i++) {
    float x = i / 0.33;
    float y = i / 0.73;

    uint32_t s  = (i * 2) % 60;       // Секунди +2 кожну ітерацію
    uint32_t m  = (i / 2) % 60;       // Хвилини +1 кожні дві ітерації
    uint32_t h  = (i / 120) % 24;     // Години (майже не змінюються при 85 ітераціях)
    uint32_t d  = 10;                 // Число (нехай буде 10-те)
    uint32_t mo = 5;                  // Травень
    uint32_t y_raw = 26;              // 2026 рік

    uint32_t packedTime = 0;
    packedTime |= (uint32_t)((y_raw - 26) & 0x3F) << 26; // Рік (26-26 = 0)
    packedTime |= (mo & 0x0F) << 22;                     // Місяць
    packedTime |= (d & 0x1F) << 17;                      // День
    packedTime |= (h & 0x1F) << 12;                      // Година
    packedTime |= (m & 0x3F) << 6;                       // Хвилина
    packedTime |= (s & 0x3F);
    
    addPairCoordinate(x, y, packedTime, file);
  }
  file.close();
  Serial.println("Data created");
}

std::vector<GPS_data> getAllCoordinate() {
  std::vector<GPS_data> coordinate_vec;
  float x, y;
  uint32_t time;
  File file = LittleFS.open(path, "r");
  while (file.available()) {
    String iter_str = file.readStringUntil('\n');
    iter_str.trim();  //
    if (iter_str.length() == 0) continue;  //
    sscanf(iter_str.c_str(), "%f %f %u", &x, &y, &time);
    coordinate_vec.push_back({x, y, time});
  }
  file.close();
  file = LittleFS.open(path, "w");
  file.close();
  return coordinate_vec;
}

void cutSendData() {
  std::vector<GPS_data> data_for_send = getAllCoordinate();
  Serial.println("Data read complete");
  while (!data_for_send.empty() && sendPermition) {
    unsigned char packetSize = (data_for_send.size() >= 20) ? 20 : data_for_send.size();
    memset(&replyData, 0, sizeof(replyData));
    replyData.count = packetSize;
    replyData.msgType = MSG_DATA;
    for (int i = 0; i < packetSize; i++) {
      replyData.coord[i][0] = data_for_send[i].x;
      replyData.coord[i][1] = data_for_send[i].y;
      replyData.time[i] = data_for_send[i].time;
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
      data_for_send.erase(data_for_send.begin(), data_for_send.begin() + packetSize);
      Serial.println("Erased data");
    } else {
      Serial.println("Not respond");
      break;
    }
    delay(20);
  }
  
  if (!data_for_send.empty()) {
    File file = LittleFS.open(path, "a");
    if (file) {
      for (const auto& p : data_for_send) {
        addPairCoordinate(p.x, p.y, p.time, file);
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