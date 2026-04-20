#ifndef CONFIG_H
#define CONFIG_H

// 1. Cấu hình kết nối WiFi
const char* WIFI_SSID = "Bao Lam 2.4G";
const char* WIFI_PASSWORD = "baolam1683";

// 2. Cấu hình ứng dụng BLYNK (Thêm mới)
#define BLYNK_TEMPLATE_ID "TMPL6tNTP8zT1"
#define BLYNK_TEMPLATE_NAME "IoT Environmental Monitoring System"
#define BLYNK_AUTH_TOKEN "idWQLlmxs60FgMosqmMTVi_j8li6fyqq"

// 3. Cấu hình Firebase Cloud
// Lưu ý: Không để https:// ở đầu host
#define FIREBASE_HOST "iot-environmental-acd0a-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "kG2Z2hVIyCnkyJbCo2jJBCRYVtpCd6HgOaREg8Zo"

// 4. Cấu hình thời gian thực (NTP)
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 25200 
#define DAYLIGHT_OFFSET_SEC 0

// 5. Đường dẫn dữ liệu Firebase (Paths)
#define PATH_LATEST "/Environment/Latest"
#define PATH_HISTORY "/Environment/History"
#define PATH_THRESHOLD "/Settings/threshold"

// 6. Cấu hình Số điện thoại & Chân cắm Module SIM 4G
#define SIM_RX 16
#define SIM_TX 17
const String ALERT_PHONE_NUMBER = "+84698595206";

// 7. Cấu hình chân cắm (Pin Mapping) cho ESP32 WROOM
#define PMS_RX 26
#define PMS_TX 27
#define SERVO_PIN 33
#define RELAY_FAN_PIN 32

// 8. Cấu hình chân I2C (Màn hình OLED & SHT41)
#define I2C_SDA 21
#define I2C_SCL 22

// 9. Thông số vận hành
const int DEFAULT_PM_THRESHOLD = 100; 

#endif