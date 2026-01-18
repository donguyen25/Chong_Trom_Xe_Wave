 /*
  TX_Wave - ESP32 TX (transmitter) unit
  Mục đích: gửi lệnh điều khiển (bật/tắt LED) qua ESP-NOW tới máy nhận (RX)
  - Nút 1 (BTN1): chế độ chống trộm (gửi id=1)
  - Nút 2 (BTN2): tìm xe (gửi id=2)
  - (Tùy chọn) BTN3: dự phòng cho tính năng thêm
  Phát triển bởi: [ Nguyễn Văn Đô]
  Ngày: [ Tháng 6, 2024 ]
  Phiên bản: 1.0
*/

#include <WiFi.h>
#include <esp_now.h>

// --- Chân phần cứng ---
#define BTN1 3  // Nút 1: Chống trộm (INPUT_PULLUP)
#define BTN2 2  // Nút 2: Tìm xe (INPUT_PULLUP)
// #define BTN3 41 // Dự phòng
#define LED_PIN 5 // LED báo hiệu (OUTPUT)

// Địa chỉ MAC của thiết bị nhận (RX) - hãy thay bằng MAC của bạn nếu cần
uint8_t rxAddress[] = {0xDC, 0x06, 0x75, 0x67, 0x63, 0x4C}; // MAC RX

// --- Giao thức dữ liệu gửi ---
// Mẫu dữ liệu đơn giản: id (button id) và state (0/1)
typedef struct {
  uint8_t id;    // id nút (1..3)
  uint8_t state; // trạng thái (bật/tắt)
} control_msg_t;

control_msg_t msg;      // buffer để gửi
bool ledState[3] = {0, 0, 0}; // trạng thái LED ảo cho từng id (được toggle khi gửi)

// --- Debounce cho nút nhấn ---
// Lưu trạng thái trước đó của nút để phát hiện cạnh thay đổi
uint8_t prevState[3]; 
// Lưu thời điểm thay đổi gần nhất để lọc nhiễu (debounce)
unsigned long lastDebounce[3];
const unsigned long debounceDelay = 50; // 50 ms debounce

// --- Hàm tiện ích ---
// Nháy LED vật lý `times` lần, mỗi lần sáng/tắt `msDelay` ms (hiển thị feedback khi bấm nút)
void blinkLED(int pin, int times, int msDelay) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msDelay);
    digitalWrite(pin, LOW);
    delay(msDelay);
  }
}

// Callback khi ESP-NOW hoàn thành gửi: in trạng thái thành công hay thất bại
void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}

// Gửi lệnh cho id cụ thể (1..3)
// Thao tác: toggle trạng thái trong ledState[], gắn vào msg, và gọi esp_now_send
void sendLED(uint8_t id) {
  // id-1 vì mảng ledState 0-based
  ledState[id - 1] = !ledState[id - 1];
  msg.id = id;
  msg.state = ledState[id - 1];
  // gửi tới địa chỉ rxAddress đã định nghĩa
  esp_now_send(rxAddress, (uint8_t *)&msg, sizeof(msg));
}

/*=========================================================== Set up ==============================================*/
void setup() {
  Serial.begin(115200);

  // Cấu hình chân vào ra
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  // pinMode(BTN3, INPUT_PULLUP); // bỏ comment nếu sử dụng BTN3
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // đảm bảo LED tắt lúc khởi động

  // Khởi tạo trạng thái trước và bộ đếm debounce
  prevState[0] = digitalRead(BTN1);
  prevState[1] = digitalRead(BTN2);
  // prevState[2] = digitalRead(BTN3);
  lastDebounce[0] = lastDebounce[1] = lastDebounce[2] = 0;

  // ESP-NOW cần chế độ WiFi STA (station)
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return; // nếu khởi tạo thất bại thì dừng ở đây
  }

  // đăng ký callback khi gửi xong
  esp_now_register_send_cb(onSent);

  // thêm peer (điểm nhận) bằng địa chỉ MAC
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, rxAddress, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.println("TX ready");
}

/*================================================================= Loop ======================================================*/
void loop() {
  int current;

  // --- Xử lý BTN1 ---
  // Đọc trạng thái hiện tại, so sánh với prevState để phát hiện cạnh thay đổi.
  // Nếu thay đổi và đã vượt quá thời gian debounce => xử lý edge.
  current = digitalRead(BTN1);
  if (current != prevState[0]) {
    if (millis() - lastDebounce[0] > debounceDelay) {
      if (current == LOW) { // nút bấm (INPUT_PULLUP => LOW khi bấm)
        sendLED(1); // gửi id 1
        blinkLED(LED_PIN, 3, 150); // feedback bằng LED vật lý
      }
      lastDebounce[0] = millis();
    }
  }
  prevState[0] = current; // cập nhật prevState

  // --- Xử lý BTN2 (tương tự BTN1) ---
  current = digitalRead(BTN2);
  if (current != prevState[1]) {
    if (millis() - lastDebounce[1] > debounceDelay) {
      if (current == LOW) {
        sendLED(2); // gửi id 2
        blinkLED(LED_PIN, 3, 150);
      }
      lastDebounce[1] = millis();
    }
  }
  prevState[1] = current;

  // --- BTN3 (để tham khảo): nếu muốn thêm, bỏ comment phần này ---
  // current = digitalRead(BTN3);
  // if (current != prevState[2]) {
  //   if (millis() - lastDebounce[2] > debounceDelay) {
  //     if (current == LOW) {
  //       sendLED(3);
  //     }
  //     lastDebounce[2] = millis();
  //   }
  // }
  // prevState[2] = current;

  delay(10); // khoảng chờ nhỏ để giảm tần số polling
}
