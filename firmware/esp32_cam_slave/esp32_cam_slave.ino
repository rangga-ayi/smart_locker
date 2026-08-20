#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

#define DEBUG_MODE false
#define DBG(x)    if(DEBUG_MODE) Serial.println(x)
#define DBGF(...) if(DEBUG_MODE) Serial.printf(__VA_ARGS__)

const char* DEFAULT_WIFI_SSID = "palembang 01";
const char* DEFAULT_WIFI_PASS = "Athallah";

#define BOTtoken "8527910174:AAE8zeIUwjyY3SuZr-IFtOwANmrGqyYcLPg"
const char* TELEGRAM_HOST = "api.telegram.org";
const int TELEGRAM_PORT = 443;
Preferences prefs;

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22
#define FLASH_LED_PIN 4

const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long TG_TIMEOUT_MS = 20000;
const int FLASH_DURASI_MS = 300;

String cmdBuffer = "";


void setup() {
  Serial.begin(9600); 
  DBG("\n[BOOT] ESP32-CAM v4.1");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!initKamera()) {
    DBG("[ERROR] Kamera gagal init! Restart...");
    delay(3000);
    ESP.restart();
  }

  if (!sambungWifi()) {
    DBG("[ERROR] WiFi gagal! Sistem tetap berjalan untuk tunggu perintah WiFi via UART...");
  }

  DBG("[BOOT] Siap menerima perintah dari ESP32 Utama.");
}

void loop() {
  // Cek koneksi WiFi secara non-blocking setiap 5 detik
  static unsigned long lastWifiCheck = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiCheck > 5000) {
    DBG("[WiFi] Terputus, mencoba reconnect...");
    sambungWifi();
    lastWifiCheck = millis();
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      cmdBuffer.trim();
      // Hanya proses jika buffer tidak kosong setelah di-trim
      if (cmdBuffer.length() > 0) {
        DBGF("[CMD] Diterima: '%s'\n", cmdBuffer.c_str());
        prosesPerintah(cmdBuffer);
      }
      cmdBuffer = "";
    } else {
      cmdBuffer += c;
      if (cmdBuffer.length() > 256) {
        cmdBuffer = ""; 
      }
    }
  }
}

void prosesPerintah(String cmd) {
  if (cmd.startsWith("PHOTO:")) {
    int titikDua1 = cmd.indexOf(':');
    int titikDua2 = cmd.indexOf(':', titikDua1 + 1); 

    if (titikDua1 == -1 || titikDua2 == -1) {
      DBG("[CMD] Format salah. Harusnya PHOTO:index:chatid");
      Serial.println("FAIL");
      return;
    }

    String idxStr = cmd.substring(titikDua1 + 1, titikDua2);
    String chatID = cmd.substring(titikDua2 + 1);
    int    lokerIdx = idxStr.toInt();

    DBGF("[FOTO] Loker %d | Chat ID: %s\n", lokerIdx + 1, chatID.c_str());

    camera_fb_t* fb = ambilFoto();
    if (!fb) {
      DBG("[FOTO] Gagal ambil frame!");
      Serial.println("FAIL");
      return;
    }

    String caption = "Bukti Foto di Loker " + String(lokerIdx + 1);
    bool berhasil = kirimFotoTelegram(fb, chatID, caption);

    esp_camera_fb_return(fb);

    if (berhasil) {
      Serial.println("OK");
      DBG("[FOTO] Berhasil dikirim.");
    } else {
      Serial.println("FAIL");
      DBG("[FOTO] Gagal kirim.");
    }
  }
  // Perintah: Perbarui kredensial WiFi
  else if (cmd.startsWith("WIFI:")) {
    int separatorIndex = cmd.indexOf(':', 5); 
    if(separatorIndex == -1) {
      DBG("[CMD] Format WiFi salah. Harusnya WIFI:ssid:pass");
      return;
    }
    String newSSID = cmd.substring(5, separatorIndex);
    String newPass = cmd.substring(separatorIndex + 1);

    prefs.begin("cam-app", false);
    prefs.putString("ssid", newSSID);
    prefs.putString("pass", newPass);
    prefs.end();

    DBG("[WiFi] Kredensial baru disimpan. Reconnect...");
    WiFi.disconnect();
    delay(500);
    
    if(sambungWifi()) {
        Serial.println("WIFI_OK");
    } else {
        Serial.println("WIFI_FAIL");
    }
  }
  else {
    DBGF("[CMD] Perintah tidak dikenal: %s\n", cmd.c_str());
  }
}

camera_fb_t* ambilFoto() {
  for (int i = 0; i < 3; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(100);
  }

  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(FLASH_DURASI_MS);

  camera_fb_t* fb = esp_camera_fb_get();

  digitalWrite(FLASH_LED_PIN, LOW);

  if (!fb) {
    DBG("[Kamera] esp_camera_fb_get() gagal.");
    return nullptr;
  }

  DBGF("[Kamera] Frame OK. Ukuran: %d bytes\n", fb->len);
  return fb;
}

bool kirimFotoTelegram(camera_fb_t* fb, String chatID, String caption) {
  WiFiClientSecure client;
  client.setInsecure(); 

  DBGF("[TG] Menghubungi %s...\n", TELEGRAM_HOST);
  if (!client.connect(TELEGRAM_HOST, TELEGRAM_PORT)) { 
    DBG("[TG] Koneksi gagal.");
    return false;
  }

  String boundary = "----LokerBoundary7890";
  String endBoundary = "\r\n--" + boundary + "--\r\n";

  String bodyStart =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
    chatID + "\r\n" +
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
    caption + "\r\n" +
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"loker.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  int totalLen = bodyStart.length() + fb->len + endBoundary.length();

  String header =
    "POST /bot" + String(BOTtoken) + "/sendPhoto HTTP/1.1\r\n"
    "Host: " + String(TELEGRAM_HOST) + "\r\n"
    "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
    "Content-Length: " + String(totalLen) + "\r\n"
    "Connection: close\r\n\r\n";
    
  client.print(header);
  client.print(bodyStart);

  uint8_t* ptr = fb->buf;
  size_t rem = fb->len;
  while(rem > 0) {
    size_t chunk = (rem > 512) ? 512 : rem;
    client.write(ptr, chunk);
    ptr += chunk;
    rem -= chunk;
  }

  client.print(endBoundary);

  unsigned long t = millis();
  String respLine = "";
  bool sukses = false;

  while (client.connected() && (millis() - t < TG_TIMEOUT_MS)) {
    if (client.available()) {
      respLine = client.readStringUntil('\n');
      DBGF("[TG] Resp: %s\n", respLine.c_str());
      // Cek jika baris pertama respons adalah "HTTP/1.1 200 OK"
      if (respLine.indexOf("200 OK") >= 0) {
        sukses = true;
        break;
      }
      // Jika ada kode error HTTP (4xx atau 5xx)
      if (respLine.indexOf("4") == 9 || respLine.indexOf("5") == 9) {
        DBG("[TG] Error HTTP.");
        break;
      }
    }
  }

  client.stop();
  return sukses;
}

bool initKamera() {
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sscb_sda = CAM_PIN_SIOD;
  config.pin_sscb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA; 
    config.jpeg_quality = 15; 
    config.fb_count = 2;
    DBG("[Kamera] PSRAM ditemukan. Resolusi SVGA.");
  } else {
    config.frame_size = FRAMESIZE_QVGA; 
    config.jpeg_quality = 15;
    config.fb_count = 1;
    DBG("[Kamera] Tanpa PSRAM. Resolusi VGA.");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    DBGF("[Kamera] Init gagal. Error: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_aec2(s, 1);
    DBG("[Kamera] Sensor dikonfigurasi.");
  }

  DBG("[Kamera] Init berhasil.");
  return true;
}

bool sambungWifi() {
  // Baca SSID dan Pass dari memori setiap kali akan konek
  prefs.begin("cam-app", true);
  String ssid = prefs.getString("ssid", DEFAULT_WIFI_SSID);
  String pass = prefs.getString("pass", DEFAULT_WIFI_PASS);
  prefs.end();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  DBGF("[WiFi] Menghubungkan ke %s...\n", ssid.c_str());

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t >= WIFI_TIMEOUT_MS) {
      DBG("[WiFi] Timeout! Gagal konek.");
      return false;
    }
    delay(500);
    if (DEBUG_MODE) Serial.print(".");

    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') {
        cmdBuffer.trim();
        if (cmdBuffer.length() > 0) prosesPerintah(cmdBuffer);
        cmdBuffer = "";
      } else {
        cmdBuffer += c;
        if (cmdBuffer.length() > 256) {
          cmdBuffer = ""; 
        }
      }
    }
  }

  DBGF("\n[WiFi] Terhubung. IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}
