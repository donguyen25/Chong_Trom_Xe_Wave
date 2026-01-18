/*
  RX_Wave_NOW - Bộ thu ESP-NOW cho cảnh báo/điều khiển xe
  - Nhận lệnh ARM/DISARM và FIND từ TX qua ESP-NOW
  - Quản lý trạng thái hệ thống: DISARMED / ARMED / ALARM
  - Sử dụng relay, còi và LED để báo hiệu trạng thái
  - Lưu/khôi phục trạng thái ARMED vào EEPROM
  Ghi chú: Một số hàm dùng `delay()` cho tín hiệu ngắn (bíp/nhấp
  nháy). Chúng phù hợp cho tín hiệu ngắn nhưng không phải là
  non-blocking cho nhiệm vụ dài hạn.
  Tác giả: Nguyễn Đô
  Ngày: 06/01/2026
  Phiên bản: 1.0  
  *********Lưu ý: Dự án vẫn trong quá trình thử nghiêm*************
  Vui lòng kiểm tra kỹ trước khi sử dụng trong thực tế!
*/

#include <WiFi.h>
#include <esp_now.h>
#include <EEPROM.h>

/* ================================= PIN MAP ================================== */
#define RELAY1_PIN 6
#define RELAY2_PIN 7
#define BUZZER_PIN 5
#define LED_IND    8
#define SW420_PIN  4

/* ==================================== FSM ===================================== */
enum SystemState {
  DISARMED = 0,
  ARMED,
  ALARM
};

SystemState systemState = DISARMED;

/* =================================== ESP-NOW MSG ============================== */
typedef struct {
  uint8_t cmd;   // 1 = ARM/DISARM, 2 = FIND
  uint8_t state;
} esp_msg_t;

esp_msg_t rxMsg;

/* ===================================== TIME CONFIG ================================ */
const unsigned long CONNECT_TIMEOUT_MS = 3000;
const unsigned long LED_BLINK_MS       = 500;
const unsigned long CONNECT_BEEP_MS    = 2000;

const unsigned long ARM_BLINK_MS       = 150;
const int ARM_BLINK_TIMES              = 1;
const int DISARM_BLINK_TIMES           = 2;
const int FIND_BLINK_TIMES             = 5;
const int VIB_BLINK_TIMES              = 10;

const unsigned long ALARM_DURATION_MS  = 3000;
const unsigned long VIB_DEBOUNCE_MS    = 800;
const unsigned long ARM_STABILIZE_MS   = 1500;  // Thời gian ổn định sau khi bật chống trộm

/* =================================== TIME VAR ================================== */
unsigned long lastRecvTime = 0;
unsigned long lastLedMillis = 0;
unsigned long alarmStartMillis = 0;
unsigned long lastVibMillis = 0;
unsigned long armStartMillis = 0;  // Lưu thời gian bắt đầu bật chống trộm

/* ================================== FLAGS ===================================== */
bool ledState = false;
bool wasConnected = false;

/* ========================== EEPROM ================================== */
#define EEPROM_SIZE 16
#define EEPROM_MAGIC 0xA5
#define ADDR_MAGIC 0
#define ADDR_STATE 1

/* ===================== NON-BLOCKING BLINK TASK ===================== */
bool blinkActive = false;
bool blinkState = false;
int blinkCount = 0;
int blinkTarget = 0;
unsigned long lastBlinkMillis = 0;

void startBlink(int times) {
  blinkActive = true;
  blinkTarget = times * 2;   // ON + OFF
  blinkCount = 0;
  blinkState = false;
  lastBlinkMillis = millis();
  digitalWrite(RELAY2_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

void handleBlink() {
  if (!blinkActive) return;

  unsigned long now = millis();
  if (now - lastBlinkMillis >= ARM_BLINK_MS) {
    lastBlinkMillis = now;
    blinkState = !blinkState;

    digitalWrite(RELAY2_PIN, blinkState);
    digitalWrite(BUZZER_PIN, blinkState);

    blinkCount++;
    if (blinkCount >= blinkTarget) {
      blinkActive = false;
      digitalWrite(RELAY2_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

/* ================================ EEPROM ============================= */
void saveState() {
  EEPROM.write(ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.write(ADDR_STATE, (uint8_t)systemState);
  EEPROM.commit();
}

void loadState() {
  if (EEPROM.read(ADDR_MAGIC) == EEPROM_MAGIC) {
    uint8_t s = EEPROM.read(ADDR_STATE);
    if (s <= ARMED) systemState = (SystemState)s;
  }
}

/* ===================================== ESP-NOW RX ============================= */
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(rxMsg)) return;
  memcpy(&rxMsg, data, sizeof(rxMsg));
  lastRecvTime = millis();

  if (rxMsg.cmd == 1) { // ARM / DISARM
    if (systemState == DISARMED) {
      systemState = ARMED;
      armStartMillis = millis();  // Ghi nhận thời gian bắt đầu
      digitalWrite(RELAY1_PIN, HIGH);
      startBlink(ARM_BLINK_TIMES);
    } else {
      systemState = DISARMED;
      digitalWrite(RELAY1_PIN, LOW);
      startBlink(DISARM_BLINK_TIMES);
    }
    saveState();
  }

  if (rxMsg.cmd == 2) { // FIND
    startBlink(FIND_BLINK_TIMES);
  }
}

/* ================================== SETUP ================================ */
void setup() {
  Serial.begin(115200);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_IND, OUTPUT);
  pinMode(SW420_PIN, INPUT_PULLUP);

  EEPROM.begin(EEPROM_SIZE);
  loadState();

  if (systemState == ARMED)
    digitalWrite(RELAY1_PIN, HIGH);

  // Power-on beep
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_register_recv_cb(onReceive);

  Serial.println("RX FSM READY (NON-BLOCKING)");
}

/* =================================== LOOP ================================= */
void loop() {
  unsigned long now = millis();

  handleBlink();

  /* -------- CONNECTION LED -------- */
  bool connected = (now - lastRecvTime < CONNECT_TIMEOUT_MS);

  if (connected) {
    digitalWrite(LED_IND, HIGH);
  } else {
    if (now - lastLedMillis >= LED_BLINK_MS) {
      lastLedMillis = now;
      ledState = !ledState;
      digitalWrite(LED_IND, ledState);
    }
  }

  /* -------- VIBRATION CHECK -------- */
  if (systemState == ARMED) {
    unsigned long timeArmed = now - armStartMillis;
    // Chỉ kiểm tra vibration sau thời gian ổn định
    if (timeArmed >= ARM_STABILIZE_MS) {
      if (digitalRead(SW420_PIN) == LOW) {
        if (now - lastVibMillis > VIB_DEBOUNCE_MS) {
          systemState = ALARM;
          alarmStartMillis = now;
          lastVibMillis = now;
          startBlink(VIB_BLINK_TIMES);
        }
      }
    }
  }

  /* -------- ALARM STATE -------- */
  if (systemState == ALARM) {
    if (now - alarmStartMillis >= ALARM_DURATION_MS) {
      systemState = ARMED;
    }
  }
}
