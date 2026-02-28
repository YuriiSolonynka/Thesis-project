#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/timer.h"
#include "esp_log.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include <LittleFS.h>
#include <esp_now.h>
#include <WiFi.h>


#define WAKEUP_PIN GPIO_NUM_33 

#define TIMER_DIVIDER         80
#define TIMER_SCALE           (80000000 / TIMER_DIVIDER)
#define TIMER_INTERVAL_SEC    20 // час до засинання
#define TEST_WITH_RELOAD      true
#define MSG_DISCOVERY 0
#define MSG_ACCEPTED  1
#define MSG_DATA      2
#define MSG_DELAY     3
#define MSG_RESUME    4

#define BUFFER_SIZE 50 

const char* path = "/myfile.txt";

typedef struct struct_message {
    float coord[30][2];
    uint8_t count;
    uint8_t msgType;
} struct_message;

typedef struct {
    struct_message buffer[BUFFER_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} CircularBuffer;

CircularBuffer cb;

struct_message incomingData;
struct_message savingData;
struct_message sendingData;

volatile bool sleepTime = false;
volatile bool isPaired = false;
volatile bool isFileOpen = false;
unsigned long lastDiscoveryTime = 0;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t senderAddress[6];

void IRAM_ATTR timer_group0_isr(void *para) {
    uint32_t timer_intr = timer_group_get_intr_status_in_isr(TIMER_GROUP_0);
    
    if (timer_intr & TIMER_INTR_T0) {
        // для роботи переривань надалі
        timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
        timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);
        // флаг для запуску сну
        sleepTime = true;
    }
}

// видалити як буде працювати все
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Прокинувся від розмикання піна (EXT0)!"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Прокинувся по таймеру"); break;
    default : Serial.printf("Звичайний запуск або інша причина: %d\n", wakeup_reason); break;
  }
}

void init_buffer(CircularBuffer *cb) {
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
    Serial.println("init bufer");
}

bool buffer_push(CircularBuffer *cb, struct_message data) {
    if (cb->count == BUFFER_SIZE) {
        return false;  // Buffer Full
    }

    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) % BUFFER_SIZE;
    cb->count++;
    return true;
}

bool buffer_pop(CircularBuffer *cb, struct_message *data) {
    if (cb->count == 0) {
        return false;  // Buffer Empty
    }

    *data = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) % BUFFER_SIZE;
    cb->count--;
    return true;
}

bool buffer_is_empty(CircularBuffer *cb) {
    return cb->count == 0;
}


void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataPtr, int len) {
  memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));
  
  // прийняття підтвердження парування
  if (!isPaired && incomingData.msgType == MSG_ACCEPTED) {
    Serial.println("Знайшов давача, отримав підтвердження");
    isPaired = true;
    
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    memcpy(senderAddress, mac, 6);
    
    if (!esp_now_is_peer_exist(senderAddress)) {
      esp_now_add_peer(&peerInfo);
    } 
  }

  // прийняття даних при паруванні
  if (isPaired && incomingData.msgType == MSG_DATA) {
    // запис в чергу через кільцевий буфе
    if (!buffer_push(&cb, incomingData)){
      // надсилаю стоп передачі
      struct_message replyData;
      replyData.msgType = MSG_DELAY;
      esp_now_send(mac, (uint8_t *) &replyData, sizeof(replyData));
    }
  }
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);

    init_buffer(&cb);

    delay(2000); //

    if (LittleFS.begin(true)) Serial.println("LittleFS змонтовано успішно!");
    else Serial.println("Помилка LittleFS навіть після спроби форматування!");

    if (esp_now_init() != ESP_OK) {
        Serial.println("Init error ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);
    // додавання піру з широкомовною для пошуку
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    delay(2000);


    timer_config_t config = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = (timer_autoreload_t)TEST_WITH_RELOAD,
        .divider = TIMER_DIVIDER,
    };
    
    // якісь налаштування таймера
    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, TIMER_INTERVAL_SEC * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_group0_isr, NULL, ESP_INTR_FLAG_IRAM, NULL);
    
    timer_start(TIMER_GROUP_0, TIMER_0);

    // налаштування сну на розмикання плюс підтягуючий резистор
    pinMode(WAKEUP_PIN, INPUT_PULLUP);
    print_wakeup_reason();
    esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, 1); 
    rtc_gpio_pullup_en(WAKEUP_PIN);
    rtc_gpio_pulldown_dis(WAKEUP_PIN);
}

void loop() {
  // видалення з черги і запис у файл
  unsigned long currentMillis = millis();
  // надсилання кожні 0.5 секунди якщо ми не в парі
    if(!isPaired) {
      if (currentMillis - lastDiscoveryTime > 500) {
      sendingData.msgType = MSG_DISCOVERY;
      sendingData.count = 0;
      esp_now_send(broadcastAddress, (uint8_t *) &sendingData, sizeof(sendingData));
      lastDiscoveryTime = currentMillis;
      }
    } 
    if(isPaired && !buffer_is_empty(&cb)) {
    File file = LittleFS.open(path, "a");
    
    if (file) {
      char string[25];
      
      while (!buffer_is_empty(&cb)) {
        
        noInterrupts(); 
        bool success = buffer_pop(&cb, &savingData);
        interrupts();

        if (success) {
          for (int i = 0; i < savingData.count; i++){
            float x = savingData.coord[i][0];
            float y = savingData.coord[i][1];
            sprintf(string, "%.6f %.6f\n", x, y);
            file.write((const uint8_t*)string, strlen(string));
          }
          Serial.println(savingData.count);
        }
      }
      file.close();

      // логіка відновлення надсилання після переповнення буфера
      memset(&sendingData, 0, sizeof(sendingData));
      sendingData.msgType = MSG_RESUME;
      sendingData.count = 0;

      esp_now_send(senderAddress, (uint8_t *) &sendingData, sizeof(sendingData));
    }
  }
    
    if (sleepTime) {
        Serial.println("Таймер спрацював! Йдемо спати...");
        Serial.flush();
        
        sleepTime = false; 
        
        esp_deep_sleep_start();
    }
}
