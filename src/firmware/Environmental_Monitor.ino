#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_SHT4x.h"
#include "PMS.h"
#include "config.h"
#include "website.h" 
#include <time.h>
#include <BlynkSimpleEsp32.h>
#include <WebServer.h>

// --- CẤU HÌNH MÀN HÌNH OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- CẤU HÌNH FIREBASE ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig fbConfig;

// --- CẤU HÌNH CẢM BIẾN & SERVO ---
Adafruit_SHT4x sht4 = Adafruit_SHT4x();
HardwareSerial SerialPMS(1);
PMS pms(SerialPMS);
PMS::DATA data;
Servo doorServo;

// --- CẤU HÌNH MODULE SIM 4G (A7680C) ---
#define SIM_RX 16
#define SIM_TX 17
HardwareSerial SerialSIM(2);
bool isSmsSent = false; 
String phoneNumber = "+84968595206";

// --- CÁC BIẾN TRẠNG THÁI HỆ THỐNG ---
unsigned long lastPush = 0;
unsigned long lastGetFirebase = 0; 
unsigned long lastLCDUpdate = 0;

int dynamic_threshold = 100; 
int auto_close_time = 20; 
int lastPos = 0; 
String mode = "AUTO";           
String manualDoorState = "CLOSE"; 
bool isFanOn = false;
unsigned long manualExpireTime = 0; 

// Biến đếm số lần thử kết nối lại WiFi
int wifi_retry_count = 0;

// Khởi tạo Local Server chạy cổng 80
WebServer localServer(80);

// --- HÀM LẤY THỜI GIAN ---
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "SYNCING...";
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%H:%M:%S %d/%m/%Y", &timeinfo);
  return String(buffer);
}

// --- HÀM GỬI TIN NHẮN SMS ---
void sendSMS(String message) {
  Serial.println(">> [SMS] Đang gửi tin nhắn cảnh báo...");
  SerialSIM.println("AT+CMGF=1"); // Đưa module về chế độ nhắn tin Text
  delay(200);
  SerialSIM.print("AT+CMGS=\"");
  SerialSIM.print(phoneNumber);
  SerialSIM.println("\"");
  delay(200);
  SerialSIM.print(message); 
  delay(200);
  SerialSIM.write(26); // Mã ASCII Ctrl+Z để gửi
  delay(3000);
  Serial.println(">> [SMS] Đã gửi thành công!");
}

void setup() {
  Serial.begin(115200);
  // FIX LỖI RÚT CÁP TYPE-C: Chờ 1 giây để mạch nhận đủ nguồn từ Adapter ngoài
  delay(1000); 

  Wire.begin(I2C_SDA, I2C_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println(F("OLED failed"));
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  configTime(7 * 3600, 0, "pool.ntp.org");
  fbConfig.host = FIREBASE_HOST;
  fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&fbConfig, &auth);
  Firebase.reconnectWiFi(true);

  if (!sht4.begin()) Serial.println("SHT41 error!");
  
  // Khởi tạo UART1 (Cảm biến bụi) và UART2 (SIM 4G)
  SerialPMS.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX);
  SerialSIM.begin(115200, SERIAL_8N1, SIM_RX, SIM_TX);
  
  doorServo.attach(SERVO_PIN);
  pinMode(RELAY_FAN_PIN, OUTPUT);
  doorServo.write(0); 
  digitalWrite(RELAY_FAN_PIN, LOW);
  Serial.println("\n--- SYSTEM READY ---");
  
  // Khởi tạo Web Server nội bộ TRƯỚC để chắc chắn nó đã mở port 80
  localServer.on("/", []() {
    localServer.send(200, "text/html", index_html);
  });
  localServer.begin();

  // IN RA IP RA CỔNG SERIAL
  Serial.println("\n==================================");
  Serial.print(">> WEB DASHBOARD IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("==================================\n");
  
  // --- HIỂN THỊ IP LÊN MÀN HÌNH OLED ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.setTextSize(1);
  display.setCursor(0, 25);
  display.println(WiFi.localIP().toString());
  display.display();
  
  delay(2500); 

  lastLCDUpdate = millis(); 

  Blynk.config(BLYNK_AUTH_TOKEN); 
}

void loop() {
  Blynk.run();
  localServer.handleClient(); 

  // --- KIỂM TRA WIFI & TỰ ĐỘNG RESET NẾU RỚT MẠNG LÂU ---
  if (WiFi.status() != WL_CONNECTED) { 
    wifi_retry_count++;
    Serial.printf(">> Đang thử kết nối lại WiFi... Lần %d/10\n", wifi_retry_count);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(1000); // Mỗi lần thử cách nhau 1 giây
    
    if (wifi_retry_count >= 10) {
      Serial.println(">> [CRITICAL] Mất kết nối WiFi quá 10s! Đang tự động Reset hệ thống...");
      ESP.restart(); 
    }
    return;
  } else {
    wifi_retry_count = 0; 
  }

  // --- 1. ĐỌC CÀI ĐẶT TỪ CLOUD ---
  if (millis() - lastGetFirebase > 1000) {
    lastGetFirebase = millis();
    bool cloudUpdated = false;

    // >> BẮT LỆNH RESET TỪ WEB DASHBOARD <<
    if (Firebase.getString(fbdo, "/Settings/command")) {
      if (fbdo.stringData() == "RESET") {
        Serial.println(">> NHẬN LỆNH REBOOT TỪ WEB. HỆ THỐNG SẼ RESET NGAY BÂY GIỜ...");
        Firebase.setString(fbdo, "/Settings/command", "NONE"); // Xóa lệnh cũ
        delay(1000);
        ESP.restart(); // LỆNH RESET PHẦN CỨNG
      }
    }

    if (Firebase.getInt(fbdo, "/Settings/threshold")) {
      if (dynamic_threshold != fbdo.intData()) { dynamic_threshold = fbdo.intData(); cloudUpdated = true; }
    }
    if (Firebase.getInt(fbdo, "/Settings/auto_close_time")) {
      if (auto_close_time != fbdo.intData()) { auto_close_time = fbdo.intData(); cloudUpdated = true; }
    }
    if (Firebase.getString(fbdo, "/Settings/door")) {
      if (manualDoorState != fbdo.stringData()) {
        manualDoorState = fbdo.stringData();
        manualExpireTime = 0; 
        cloudUpdated = true;
      }
    }
    if (cloudUpdated) {
      Serial.printf(">> [SYNC] Limit: %d | Timer: %ds | DoorCmd: %s\n", dynamic_threshold, auto_close_time, manualDoorState.c_str());
    }
  } 

  // --- 2. ĐỌC CẢM BIẾN ---
  sensors_event_t humidity, temp;
  sht4.getEvent(&humidity, &temp);
  
  static int pm25 = 0; 
  if (pms.read(data)) {
    pm25 = data.PM_AE_UG_2_5;
  }
  String currentTime = getFormattedTime();

  // --- 3. LOGIC ĐIỀU KHIỂN THÔNG MINH & CẢNH BÁO SMS ---
  int thresholdHigh = dynamic_threshold;
  int thresholdLow = dynamic_threshold - 10; 

  if (pm25 > thresholdHigh) {
    if (!isSmsSent) {
      String smsText = "ALERT: Bui min vuot nguong an toan (" + String(pm25) + "ug/m3). He thong dang tu dong dong cua!";
      sendSMS(smsText);
      isSmsSent = true; 
    }

    if (manualDoorState == "CLOSE") {
      mode = "AUTO (PROTECT)";
      if(lastPos != 0) { doorServo.write(0); lastPos = 0; }
      isFanOn = true;
      manualExpireTime = 0;
    } else {
      mode = "AUTO (OVERRIDE)";
      if(lastPos != 90) { doorServo.write(90); lastPos = 90; }
      isFanOn = true; 
      if (manualExpireTime == 0) {
        manualExpireTime = millis() + (auto_close_time * 1000);
        Serial.printf(">> WARNING! High Dust! Door will auto-close in %ds\n", auto_close_time);
      }
    }
  } 
  else if (pm25 < thresholdLow) {
    isSmsSent = false; 
    
    mode = "MANUAL";
    if (manualDoorState == "OPEN") {
      if(lastPos != 90) { doorServo.write(90); lastPos = 90; }
      isFanOn = false;
    } else {
      if(lastPos != 0) { doorServo.write(0); lastPos = 0; }
      isFanOn = false;
    }
    manualExpireTime = 0;
  }

  if (manualExpireTime != 0 && millis() > manualExpireTime) {
    Serial.println(">> TIMEOUT: Safe closing initiated.");
    manualDoorState = "CLOSE";
    Firebase.setString(fbdo, "/Settings/door", "CLOSE"); 
    manualExpireTime = 0;
  }

  digitalWrite(RELAY_FAN_PIN, isFanOn ? HIGH : LOW);

  // --- 4. CẬP NHẬT OLED & SERIAL ---
  if (millis() - lastLCDUpdate > 1000) {
    lastLCDUpdate = millis();
    Serial.printf("[%s] PM2.5:%d | Door:%s | Fan:%s | Mode:%s\n", 
                  currentTime.c_str(), pm25, (lastPos == 90 ? "OPEN" : "CLOSE"), (isFanOn ? "ON" : "OFF"), mode.c_str());
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(currentTime);
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    display.setCursor(0, 15);
    display.printf("T:%.1fC  H:%.0f%%", temp.temperature, humidity.relative_humidity);
    display.setCursor(0, 28);
    display.print("PM2.5:");
    display.setTextSize(2);
    display.setCursor(45, 32);
    display.print(pm25);
    display.setTextSize(1);
    display.setCursor(0, 54);
    display.printf("Lim:%d %s", dynamic_threshold, (manualExpireTime > 0 ? "!!WARN!!" : "SAFE"));
    display.display();
  }

  // --- 5. PUSH DỮ LIỆU LÊN FIREBASE ---
  
  // 5.1 Đẩy dữ liệu Real-time cho Web (Ghi đè mỗi 3 giây)
  if (millis() - lastPush > 2000) { 
      lastPush = millis();
      FirebaseJson json;
      json.set("temp", temp.temperature);
      json.set("hum", humidity.relative_humidity);
      json.set("pm25", pm25);
      json.set("fan", isFanOn ? "ON" : "OFF");
      json.set("door", lastPos == 90 ? "OPEN" : "CLOSE");
      json.set("mode", mode);
      json.set("time_str", currentTime);

      int remain = 0;
      if (manualExpireTime > millis()) {
          remain = (manualExpireTime - millis()) / 1000;
      }
      json.set("remaining", remain); 

      Firebase.setJSON(fbdo, "/Environment/Latest", json);
  }

  // 5.2 Đẩy dữ liệu Lịch sử (Tạo dòng mới mỗi 5 phút = 300000ms)
  static unsigned long lastHistoryPush = 0;
  if (millis() - lastHistoryPush > 300000) { 
      lastHistoryPush = millis();
      
      FirebaseJson historyJson;
      historyJson.set("temp", temp.temperature);
      historyJson.set("hum", humidity.relative_humidity);
      historyJson.set("pm25", pm25);
      
      // Bổ sung các thông số quan trọng cho báo  CSV
      historyJson.set("limit", dynamic_threshold); 
      historyJson.set("door", lastPos == 90 ? "OPEN" : "CLOSE");
      historyJson.set("time_str", currentTime);
      
      // BẮT BUỘC có hàm time() lấy UNIX Timestamp
      time_t now;
      time(&now);
      historyJson.set("timestamp", now); 
      
      // Dùng lệnh pushJSON để tạo ra 1 bản ghi mới
      if (Firebase.pushJSON(fbdo, "/Environment/History", historyJson)) {
          Serial.println("\n========================================");
          Serial.println(">> [FIREBASE] ĐÃ LƯU BẢN GHI VÀO LỊCH SỬ !");
          Serial.printf("   - Thời gian:      %s\n", currentTime.c_str());
          Serial.printf("   - Nhiệt độ:       %.1f °C\n", temp.temperature);
          Serial.printf("   - Độ ẩm:          %.0f %%\n", humidity.relative_humidity);
          Serial.printf("   - Bụi mịn PM2.5:  %d µg/m3\n", pm25);
          Serial.printf("   - Ngưỡng (Limit): %d\n", dynamic_threshold);
          Serial.printf("   - Cửa (Servo):    %s\n", (lastPos == 90 ? "OPEN" : "CLOSE"));
          Serial.println("========================================\n");
      } else {
          Serial.println(">> [FIREBASE] LỖI LƯU LỊCH SỬ: " + fbdo.errorReason());
      }
  }
}