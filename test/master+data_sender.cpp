#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <LittleFS.h>

volatile bool deliveryFinished = false;
volatile bool deliverySuccess = false;
uint8_t broadcastAddress[] = {0x1c, 0x69, 0x20, 0xc6, 0xbd, 0x0c};
const char* path = "/myfile.txt";

typedef struct struct_message {
  float coord[30][2];
  unsigned char count;
} struct_message;

struct_message myData;

esp_now_peer_info_t peerInfo;


void addPairCoordinate (float x, float y, File file) {
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
  while (!coord.empty()) {
    unsigned char packetSize = (coord.size() >= 30) ? 30 : coord.size();
    memset(&myData, 0, sizeof(myData));
    myData.count = packetSize;
    for (int i = 0; i < packetSize; i++) {
      myData.coord[i][0] = coord[i].first;
      myData.coord[i][1] = coord[i].second;
    }

    deliveryFinished = false;
    deliverySuccess = false;

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

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
    Serial.println("All data sent");
    delay(40); 
  }
}


void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  deliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
  deliveryFinished = true;
}


void setup() {
  Serial.begin(115200);
 
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  if (LittleFS.begin()) Serial.println("++");
  

  create_data();
  delay(5000);
  cutSendData();
}

 
void loop() {
}
