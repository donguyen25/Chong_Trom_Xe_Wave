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

/*============================ Các thư viện cần thiết ============================*/
#include <WiFi.h>
#include <esp_now.h>
#include <EEPROM.h>

/* ================================= PIN MAP ================================== */
#define RELAY1_PIN 9    // Đề xe
#define RELAY2_PIN 10   // Xi Nhan
#define BUZZER_PIN 2    // Còi Báo Động
#define LED_IND    3    // LED Chỉ Thị Kết Nối
#define SW420_PIN  4    // Cảm Biến Rung 

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
  uint8_t state; // optional
} esp_msg_t;

esp_msg_t rxMsg;

/* ===================================== TIME CONFIG ================================ */
const unsigned long CONNECT_TIMEOUT_MS = 3000;  //Phát hiện mất kết nối TX
const unsigned long LED_BLINK_MS       = 500;   //  Chu kỳ LED nhấp nháy
const unsigned long CONNECT_BEEP_MS    = 2000;  //  Thời gian Bíp khi kết nối lại

const unsigned long ARM_BLINK_MS       = 150; // Thời gian nhấp nháy khi ARM/DISARM
const int ARM_BLINK_TIMES              = 3;   // Số lần nhấp nháy khi ARM/DISARM
const int FIND_BLINK_TIMES             = 5;   // Số lần nhấp nháy khi FIND
const int VIB_BLINK_TIMES              = 10;  // Số lần nhấp nháy khi Rung Phát Hiện

const unsigned long ALARM_DURATION_MS  = 3000; // Thời gian báo động
const unsigned long VIB_DEBOUNCE_MS    = 800;  // Thời gian chống chớp khi cảm biến rung kích hoạt

/* =================================== TIME VAR ================================== */
unsigned long lastRecvTime = 0;
unsigned long lastLedMillis = 0;
unsigned long alarmStartMillis = 0;
unsigned long lastVibMillis = 0;

/* ================================== FLAGS ===================================== */
bool ledState = false;
bool wasConnected = false;

/* ========================== EEPROM ================================== */
#define EEPROM_SIZE 16
#define EEPROM_MAGIC 0xA5
#define ADDR_MAGIC 0
#define ADDR_STATE 1

/* ===================== HELPER: Blink relay2 + buzzer ===================== */
// Hàm này tạo các xung bật/tắt ngắn cho RELAY2 và còi.
// Lưu ý: sử dụng `delay()` nên hoạt động mang tính blocking ngắn,
// phù hợp cho các tín hiệu cảnh báo/hiển thị nhỏ.
void blinkRelay2Buzzer(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(RELAY2_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(ARM_BLINK_MS);
    digitalWrite(RELAY2_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    delay(ARM_BLINK_MS);
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

// Lưu ý: `saveState()` và `loadState()` đảm bảo trạng thái ARM/DISARM
// được duy trì qua khởi động lại bằng EEPROM.

/* ===================================== ESP-NOW RX ============================= */
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(rxMsg)) return;
  memcpy(&rxMsg, data, sizeof(rxMsg));
  lastRecvTime = millis();

  if (rxMsg.cmd == 1) { // ARM / DISARM
    // Khi nhận lệnh ARM/DISARM:
    // - Nếu đang DISARMED -> chuyển sang ARMED và bật RELAY1 (nguồn đề)
    // - Nếu đang ARMED/ALARM -> về DISARMED và tắt RELAY1
    if (systemState == DISARMED) {
      systemState = ARMED;
      digitalWrite(RELAY1_PIN, HIGH);
      blinkRelay2Buzzer(ARM_BLINK_TIMES);
    } else {
      systemState = DISARMED;
      digitalWrite(RELAY1_PIN, LOW);
      // khi tắt (DISARM) cũng nhấp nháy relay2 + còi 3 lần
      blinkRelay2Buzzer(ARM_BLINK_TIMES);
      digitalWrite(RELAY2_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
    }
    saveState();
  }

  if (rxMsg.cmd == 2) { // FIND
    // Lệnh tìm xe: tạo vài lần nháy/tiếng để dễ xác định vị trí
    blinkRelay2Buzzer(FIND_BLINK_TIMES);
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

  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_register_recv_cb(onReceive);

  Serial.println("RX FSM READY");
}

// Thiết lập phần cứng và ESP-NOW
// - Khởi tạo các chân I/O, EEPROM và trạng thái ban đầu
// - Đăng ký callback nhận dữ liệu `onReceive`

/* =================================== LOOP ================================= */
void loop() {
  unsigned long now = millis();

  /* -------------- CONNECTION LED ---------- */
  bool connected = (now - lastRecvTime < CONNECT_TIMEOUT_MS);

  if (connected) {
    if (!wasConnected) {
      wasConnected = true;
      digitalWrite(BUZZER_PIN, HIGH);
      delay(CONNECT_BEEP_MS);
      digitalWrite(BUZZER_PIN, LOW);
    }
    digitalWrite(LED_IND, HIGH);
  } else {
    if (wasConnected) {
      saveState();
      wasConnected = false;
    }
    if (now - lastLedMillis >= LED_BLINK_MS) {
      lastLedMillis = now;
      ledState = !ledState;
      digitalWrite(LED_IND, ledState);
    }
  }

  // Giải thích logic kết nối:
  // - Nếu trong khoảng thời gian `CONNECT_TIMEOUT_MS` có gói nhận -> coi là connected
  // - Khi mới kết nối lại sẽ tạo 1 bíp dài để báo
  // - Khi mất kết nối, LED sẽ nhấp nháy để báo trạng thái mất TX

  /* ---------------- VIBRATION CHECK ------------- */
  if (systemState == ARMED) {
    if (digitalRead(SW420_PIN) == LOW) {
      if (now - lastVibMillis > VIB_DEBOUNCE_MS) {
        systemState = ALARM;
        alarmStartMillis = now;
        lastVibMillis = now;
        // blink relay2 and buzzer VIB_BLINK_TIMES times
        blinkRelay2Buzzer(VIB_BLINK_TIMES);
      }
    }
  }

  // Kiểm tra rung (SW420): chỉ kích hoạt khi ARMED và có debounce

  /* ------------------ ALARM STATE ----------------- */
  if (systemState == ALARM) {
    if (now - alarmStartMillis >= ALARM_DURATION_MS) {
      digitalWrite(RELAY2_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
      systemState = ARMED;
    }
  }
}
