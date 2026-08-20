#include <SPI.h>
#include <MFRC522.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <UniversalTelegramBot.h>
#include <Adafruit_PCF8574.h>
#include <Adafruit_VL53L0X.h>
#include <esp_wifi.h>

#define DEBUG_MODE false
#define DBG(x) if(DEBUG_MODE) Serial.println(x)
#define DBGF(...) if(DEBUG_MODE) Serial.printf(__VA_ARGS__)

#define RELAY_ON LOW
#define RELAY_OFF HIGH

#define SPI_SCK 19
#define SPI_MISO 5
#define SPI_MOSI 18
#define SS_PIN 23
#define RST_PIN 2
#define CAM_RX 3
#define CAM_TX 1

const int JUMLAH_LOKER = 3;
const int MAX_PIN_LENGTH = 8;
const String ADMIN_PIN_DEFAULT = "9999";

const unsigned long DURASI_BUKA = 10000;
const unsigned long INPUT_TIMEOUT = 15000;
const unsigned long DURASI_PESAN_LCD = 5000;
const unsigned long CAM_TIMEOUT = 25000; 
const unsigned long FOTO_POSE_DELAY = 5000;
const unsigned long DEBOUNCE_PINTU = 1500;
const unsigned long KONFIRMASI_TIMEOUT = 600000;
const unsigned long PORTAL_TIMEOUT_MS = 600000;
const unsigned long LCD_TIMEOUT = 600000; 

const int JARAK_KOSONG_MIN = 270;
const int JARAK_PENUH_MAX = 60;

MFRC522 mfrc522(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_PCF8574 pcf;
Adafruit_VL53L0X vl53[JUMLAH_LOKER];

const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'}, {'4','5','6','B'},
  {'7','8','9','C'}, {'*','0','#','D'}
};
byte rowPins[ROWS] = {15, 13, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

const int relayPins[JUMLAH_LOKER] = {4, 16, 17};
const int xshutPins[JUMLAH_LOKER] = {0, 2, 4};
const int mc38Pins[JUMLAH_LOKER] = {1, 3, 5};
const uint8_t vl53Addr[JUMLAH_LOKER] = {0x30, 0x31, 0x32};

#define BOTtoken "8527910174:AAE8zeIUwjyY3SuZr-IFtOwANmrGqyYcLPg"

WiFiClientSecure clientPoll;
UniversalTelegramBot botPoll(BOTtoken, clientPoll);

WiFiClientSecure clientSend;
UniversalTelegramBot botSend(BOTtoken, clientSend);

struct PesanTelegram {
  String chatID;
  String pesan;
  String keyboard;
};
QueueHandle_t qPesanTelegram;
SemaphoreHandle_t xSendMutex;
Preferences prefs;

enum StatusIsiLoker { LOKER_KOSONG, LOKER_TERISI_SEBAGIAN, LOKER_PENUH };

enum FaseLoker {
  FASE_IDLE,
  FASE_TUNGGU_KAMERA,
  FASE_TUNGGU_KONFIRMASI,
  FASE_PINTU_TERBUKA,
  FASE_TUNGGU_PINTU_TUTUP,
  FASE_STABILISASI_SENSOR
};

enum TipeAkses { AKSES_KURIR, AKSES_PENGHUNI };

FaseLoker lockerPhase[JUMLAH_LOKER];
TipeAkses tipeAkses[JUMLAH_LOKER];
unsigned long timerFase[JUMLAH_LOKER];

struct DataKamar {
  String uid;
  String roomPin;
  String chatID;
  String otpAktif;
  StatusIsiLoker statusIsi;
  bool kurirBolehMasuk;
};
DataKamar kamar[JUMLAH_LOKER];

String inputPin = "";
bool statusWiFiSebelumnya = false;
unsigned long waktuTolak = 0;
bool sedangTolak = false;
unsigned long waktuInputTerakhir = 0;
String camBuffer = "";

bool isLcdAsleep = false;
unsigned long waktuAktivitasTerakhir = 0;

volatile bool flagReset[JUMLAH_LOKER] = {false, false, false};
volatile bool flagStatus[JUMLAH_LOKER] = {false, false, false};
volatile bool flagKonfirmasiDiterima[JUMLAH_LOKER] = {false, false, false};
volatile bool flagKonfirmasiDitolak[JUMLAH_LOKER] = {false, false, false};

enum AdminState {
  ADMIN_NONE,
  ADMIN_WAIT_PIN,
  ADMIN_MENU,
  ADMIN_SELECT_ROOM_RFID,
  ADMIN_SCAN_CARD,
  ADMIN_SELECT_ROOM_TG,
  ADMIN_INPUT_TG,
  ADMIN_SELECT_ROOM_RESET,
  ADMIN_CONFIRM_RESET,
  ADMIN_CHANGE_PIN,
  ADMIN_SET_MASTER_CARD
};
AdminState adminState = ADMIN_NONE;
int adminTargetRoom = -1;
String adminInputBuf = "";
String adminPin = "";
String masterUid = "";

void initVL53();
void initDatabase();
void startWifiPortal();
void checkAccess(String input, String type);
void mulaiAlurKamera(int idx, TipeAkses tipe);
void handleFaseLoker();
void kirimKonfirmasiKeyboard(int idx, bool fotoSukses = true);
bool isDoorClosed(int idx);
int bacaJarak(int idx);
void showIdleMessage();
String readUID();
void updateLoker(int idx, StatusIsiLoker status, String otp);
void kirimPesan(String chatID, String pesan, String keyboard = "");
void taskTelegramSender(void* param);
void taskPollingTelegram(void* param);
void showLcdError(String line1, String line2 = "");
void handleTelegramFlags();
void handleWiFiStatusChange();
void handleInputTimeout();
void handleLcdSleep();
void wakeUpLcd();
void handleKeypadInput();
void handleRfidInput();
void triggerAdminMode();
void exitAdminMode();
void showAdminMenu();
void handleAdminKey(char key);
void adminRegisterCard(String uid);
void recoverI2CAndSensors();
void checkI2CHealth();
void clearI2CBus();

void setup() {
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], RELAY_OFF);
  }

  if (DEBUG_MODE) Serial.begin(115200);
  DBG("\n[BOOT] Sistem Loker v4.2");

  Serial2.begin(9600, SERIAL_8N1, CAM_RX, CAM_TX);
  DBG("[BOOT] Serial2 ESP32-CAM aktif.");

  Wire.begin(21, 22);
  Wire.setClock(50000); 
  Wire.setTimeOut(150); 
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SS_PIN);
  mfrc522.PCD_Init();
  lcd.init();
  lcd.backlight();
  lcd.print("Memulai...");

  if (!pcf.begin(0x20, &Wire)) {
    DBG("[ERROR] PCF8574 gagal!");
    lcd.setCursor(0, 1); lcd.print("ERR: PCF8574");
    while (1);
  }
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    pcf.pinMode(xshutPins[i], OUTPUT);
    pcf.pinMode(mc38Pins[i], INPUT_PULLUP);
    pcf.digitalWrite(xshutPins[i], LOW);
  }
  delay(10);
  initVL53();

  for (int i = 0; i < JUMLAH_LOKER; i++) {
    lockerPhase[i] = FASE_IDLE;
    timerFase[i] = 0;
    DBGF("[Boot] Pintu loker %d: %s\n", i+1, isDoorClosed(i) ? "TERTUTUP" : "TERBUKA");
  }

  prefs.begin("loker-app", false);
  initDatabase();

  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  WiFi.mode(WIFI_STA);
  
  WiFi.setAutoReconnect(true);
  if (ssid.length() > 0) WiFi.begin(ssid.c_str(), pass.c_str());
  else WiFi.begin("palembang 01", "Athallah");
  clientPoll.setInsecure();
  clientPoll.setTimeout(5000);

  clientSend.setInsecure();
  clientSend.setTimeout(15000);

  xSendMutex = xSemaphoreCreateMutex();

  qPesanTelegram = xQueueCreate(10, sizeof(PesanTelegram*));
  xTaskCreatePinnedToCore(taskTelegramSender, "TgSend", 8192, NULL, 2, NULL, 1);

  xTaskCreatePinnedToCore(taskPollingTelegram, "TGPoll", 10240, NULL, 1, NULL, 0);

  lcd.clear(); lcd.print("Sistem Loker v4");
  delay(2000);

  String currentSSID = ssid;
  String currentPass = pass;
  if (currentSSID == "") {
    currentSSID = "palembang 01";
    currentPass = "Athallah";
  }
  String wifiCmd = "WIFI:" + currentSSID + ":" + currentPass + "\n";
  Serial2.print(wifiCmd);
  DBGF("[BOOT] Sync WiFi ke CAM: %s\n", currentSSID.c_str());
  delay(500);

  showIdleMessage();
  DBG("[BOOT] Selesai.");
  waktuAktivitasTerakhir = millis();
}

void loop() {
  checkI2CHealth();
  handleTelegramFlags();
  handleWiFiStatusChange();
  handleFaseLoker();
  handleInputTimeout();
  handleLcdSleep();

  handleRfidInput();
  handleKeypadInput();

  vTaskDelay(pdMS_TO_TICKS(10));
}

void handleTelegramFlags() {
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    if (flagReset[i]) {
      flagReset[i] = false;
      kamar[i].kurirBolehMasuk = true;
      prefs.putBool(("kb" + String(i)).c_str(), true);
      lockerPhase[i] = FASE_IDLE;
      DBGF("[TG] /reset kamar %d\n", i+1);
      kirimPesan(kamar[i].chatID, "Sistem loker kamar " + String(i+1) + " telah direset paksa ke posisi standby.\nKurir bisa mencoba memasukkan PIN kembali.");
      showIdleMessage();
    }

    if (flagStatus[i]) {
      flagStatus[i] = false;
      String statusIsiStr = "Kosong";
      int jarakAktual = bacaJarak(i);
      if (kamar[i].statusIsi == LOKER_TERISI_SEBAGIAN) statusIsiStr = "Terisi Sebagian";
      else if (kamar[i].statusIsi == LOKER_PENUH) statusIsiStr = "Penuh";

      String msg = "Status Loker " + String(i+1) + ":\n";
      msg += "Isi: " + statusIsiStr + "\n";
      msg += "Jarak Terbaca: " + String(jarakAktual) + " mm\n";
      msg += "Akses Kurir: Terbuka (Via Kamera & Konfirmasi)";
      kirimPesan(kamar[i].chatID, msg);
    }
  }
}

void handleWiFiStatusChange() {
  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (wifiNow != statusWiFiSebelumnya) {
    statusWiFiSebelumnya = wifiNow;
    DBGF("[WiFi] %s\n", wifiNow ? "ONLINE" : "OFFLINE");
    showIdleMessage();
  }
}

void handleInputTimeout() {
  if (sedangTolak && (millis() - waktuTolak >= DURASI_PESAN_LCD)) {
    sedangTolak = false;
    showIdleMessage();
  }

  if (inputPin != "" && (millis() - waktuInputTerakhir > INPUT_TIMEOUT)) {
    inputPin = "";
    showIdleMessage();
  }
}

void handleRfidInput() {
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    wakeUpLcd();
    String uid = readUID();
    DBGF("[RFID] UID: %s\n", uid.c_str());
    if (adminState == ADMIN_SCAN_CARD || adminState == ADMIN_SET_MASTER_CARD) {
      adminRegisterCard(uid);
    } else if (adminState == ADMIN_NONE) {
      if (uid == masterUid && masterUid != "") {
        lcd.clear(); lcd.print("Kartu Master OK!");
        delay(1500);
        adminInputBuf = "";
        showAdminMenu();
      } else if (!sedangTolak) {
        checkAccess(uid, "RFID");
      }
    }
    mfrc522.PICC_HaltA();
  }
}

void handleKeypadInput() {
  char key = keypad.getKey();
  if (!key) return;

  if (isLcdAsleep) {
    wakeUpLcd();
    return; 
  }
  wakeUpLcd(); 

  if (adminState != ADMIN_NONE) {
    handleAdminKey(key);
    return;
  }

  waktuInputTerakhir = millis();

  if (key == 'A') {
    triggerAdminMode();
  } else if (key == '#') {
    if (inputPin.length() < 2) {
      showLcdError("Akses Ditolak!", "Min. 2 digit");
    } else {
      checkAccess(inputPin, "KEYPAD");
    }
    inputPin = "";
  } else if (key == '*') {
    inputPin = ""; 
    for (int i = 0; i < JUMLAH_LOKER; i++) {
      if (lockerPhase[i] == FASE_TUNGGU_KAMERA || lockerPhase[i] == FASE_TUNGGU_KONFIRMASI) {
        lockerPhase[i] = FASE_IDLE; 
        DBGF("[Loker %d] Operasi dibatalkan user (*).\n", i+1);
      }
    }
    showIdleMessage();
  } else if (key == 'B') {
    if (inputPin.length() > 0) {
      inputPin.remove(inputPin.length() - 1);
      lcd.setCursor(0, 1); lcd.print("                ");
      if (inputPin.length() > 0) {
        lcd.setCursor(0, 1); lcd.print("Input: " + inputPin);
      } else {
        showIdleMessage();
      }
    }
  } else if (key != 'C' && key != 'D') {
    if ((int)inputPin.length() < MAX_PIN_LENGTH) {
      inputPin += key;
      lcd.setCursor(0, 1); lcd.print("                ");
      lcd.setCursor(0, 1); lcd.print("Input: " + inputPin);
    }
  }
}

void handleFaseLoker() {
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    switch (lockerPhase[i]) {
      case FASE_TUNGGU_KAMERA: {
        while (Serial2.available()) {
          char c = Serial2.read();
          if (c == '\n') {
            camBuffer.trim();
            DBGF("[CAM] Loker %d respons: %s\n", i+1, camBuffer.c_str());
            
            bool fotoSukses = (camBuffer.indexOf("OK") >= 0); 
            camBuffer = "";
            
            if (tipeAkses[i] == AKSES_KURIR) {
              kirimKonfirmasiKeyboard(i, fotoSukses);
              lockerPhase[i] = FASE_TUNGGU_KONFIRMASI;
              timerFase[i] = millis();
              lcd.clear(); lcd.print("Menunggu");
              lcd.setCursor(0, 1); lcd.print("Konfirmasi TG...");
              DBGF("[Loker %d] Menunggu konfirmasi penghuni.\n", i+1);
            } else {
              digitalWrite(relayPins[i], RELAY_ON);
              lockerPhase[i] = FASE_PINTU_TERBUKA;
              timerFase[i] = millis();
              lcd.clear(); lcd.print("Akses Diterima!");
              lcd.setCursor(0, 1); lcd.print("Ambil Paket Anda");
              DBGF("[Loker %d] Relay ON (penghuni)\n", i+1);
            }
            break;
          } else {
            camBuffer += c;
            if (camBuffer.length() > 256) {
              camBuffer = ""; 
            }
          }
        }

        if (millis() - timerFase[i] >= CAM_TIMEOUT) {
          DBG("[CAM] Timeout!");
          camBuffer = "";

          if (tipeAkses[i] == AKSES_KURIR) {
            kirimKonfirmasiKeyboard(i, false); 
            lockerPhase[i] = FASE_TUNGGU_KONFIRMASI;
            timerFase[i] = millis();
            lcd.clear(); lcd.print("Menunggu");
            lcd.setCursor(0, 1); lcd.print("Konfirmasi TG...");
          } else {
            digitalWrite(relayPins[i], RELAY_ON);
            lockerPhase[i] = FASE_PINTU_TERBUKA;
            timerFase[i] = millis();
            lcd.clear(); lcd.print("Akses Diterima!");
            lcd.setCursor(0, 1); lcd.print("Ambil Paket Anda");
          }
        }
        break;
      }
      case FASE_TUNGGU_KONFIRMASI: {
        if (flagKonfirmasiDiterima[i]) {
          flagKonfirmasiDiterima[i] = false;
          DBGF("[Loker %d] Konfirmasi DITERIMA.\n", i+1);
          digitalWrite(relayPins[i], RELAY_ON);
          lockerPhase[i] = FASE_PINTU_TERBUKA;
          timerFase[i] = millis();
          lcd.clear(); lcd.print("Akses Diterima!");
          lcd.setCursor(0, 1); lcd.print("Masukkan Paket");
          break;
        }

        if (flagKonfirmasiDitolak[i]) {
          flagKonfirmasiDitolak[i] = false;
          DBGF("[Loker %d] Konfirmasi DITOLAK.\n", i+1);
          showLcdError("Akses Ditolak!", "Ditolak penghuni");
          lockerPhase[i] = FASE_IDLE;
          break;
        }

        if (millis() - timerFase[i] >= KONFIRMASI_TIMEOUT) {
          DBGF("[Loker %d] Timeout konfirmasi.\n", i+1);
          kirimPesan(kamar[i].chatID, "Waktu konfirmasi loker " + String(i+1) + " habis.\nKurir perlu mencoba kembali.");
          showLcdError("Waktu Habis!", "Tidak ada respon");
          lockerPhase[i] = FASE_IDLE;
        }
        break;
      }

      case FASE_PINTU_TERBUKA:
        if (pcf.digitalRead(mc38Pins[i]) == HIGH || millis() - timerFase[i] >= DURASI_BUKA) {
          digitalWrite(relayPins[i], RELAY_OFF);
          lockerPhase[i] = FASE_TUNGGU_PINTU_TUTUP;
          timerFase[i] = millis();
          DBGF("[Loker %d] Relay OFF, tunggu MC-38\n", i+1);
          lcd.clear(); lcd.print("Tutup pintu...");
          lcd.setCursor(0, 1); lcd.print("Loker " + String(i+1));
        }
        break;

      case FASE_TUNGGU_PINTU_TUTUP:
        if (isDoorClosed(i)) {
          lockerPhase[i] = FASE_STABILISASI_SENSOR;
          timerFase[i] = millis();
          DBGF("[Loker %d] MC-38: pintu tertutup\n", i+1);
        }
        break;

      case FASE_STABILISASI_SENSOR:
        if (millis() - timerFase[i] >= DEBOUNCE_PINTU) {
          int jarak = bacaJarak(i);
          
          if (jarak == -1) {
            DBGF("[Loker %d] Sensor error (I2C Hang)! Memulai auto-recovery...\n", i+1);
            recoverI2CAndSensors();
            timerFase[i] = millis(); 
            break; 
          }
          
          StatusIsiLoker newStatus;
          String statusStr;
          
          if (jarak > JARAK_KOSONG_MIN) {
            newStatus = LOKER_KOSONG;
            statusStr = "KOSONG";
          } else if (jarak < JARAK_PENUH_MAX) {
            newStatus = LOKER_PENUH;
            statusStr = "PENUH";
          } else {
            newStatus = LOKER_TERISI_SEBAGIAN;
            statusStr = "TERISI SEBAGIAN";
          }
          DBGF("[Loker %d] VL53: %dmm -> Status: %s\n", i+1, jarak, statusStr.c_str());

          if (tipeAkses[i] == AKSES_KURIR) {
            if (newStatus != LOKER_KOSONG) {
              String newOtp = String((esp_random() % 9000) + 1000);
              updateLoker(i, newStatus, newOtp);
              kamar[i].kurirBolehMasuk = false;
              prefs.putBool(("kb" + String(i)).c_str(), false);
              
              String msg = "Paket masuk ke loker kamar " + String(i + 1) + "!\nStatus Loker: *" + statusStr + "*\nGunakan kode OTP berikut untuk mengambil:\n*" + newOtp + "*\n\nAtau tempelkan kartu RFID / e-KTP Anda.";
              kirimPesan(kamar[i].chatID, msg);
              DBGF("[Loker %d] Pesan OTP dikirim ke queue.\n", i+1);
            } else {
              updateLoker(i, LOKER_KOSONG, "");
              kamar[i].kurirBolehMasuk = true;
              prefs.putBool(("kb" + String(i)).c_str(), true);
              DBGF("[Loker %d] Kurir gagal menaruh paket (sensor baca kosong).\n", i+1);
              kirimPesan(kamar[i].chatID, "Kurir mencoba memasukkan paket ke loker " + String(i+1) + " namun gagal. Loker masih kosong.");
            }
          } else {
            if (newStatus == LOKER_KOSONG) {
              updateLoker(i, LOKER_KOSONG, "");
              kamar[i].kurirBolehMasuk = true;
              prefs.putBool(("kb" + String(i)).c_str(), true);
              DBGF("[Loker %d] Kosong terkonfirmasi VL53.\n", i+1);
              kirimPesan(kamar[i].chatID, "Paket kamar " + String(i+1) + " telah diambil.\nLoker kembali kosong dan siap digunakan.");
            } else {
              updateLoker(i, newStatus, kamar[i].otpAktif);
              kamar[i].kurirBolehMasuk = false;
              prefs.putBool(("kb" + String(i)).c_str(), false);
              DBGF("[Loker %d] VL53 masih baca barang setelah penghuni.\n", i+1);
              kirimPesan(kamar[i].chatID, "Peringatan: Anda belum mengambil semua barang dari loker " + String(i+1) + ".\nStatus loker saat ini: *" + statusStr + "*.\nSilakan gunakan kartu RFID Anda untuk membuka kembali.");
            }
          }
          
          lockerPhase[i] = FASE_IDLE;
          showIdleMessage();
        }
        break;

      case FASE_IDLE:
      default:
        break;
    }
  }
}

void checkAccess(String input, String type) {
  int idx = -1;
  bool aksesDiterima = false;
  TipeAkses tipe = AKSES_KURIR;
  
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    if (type == "KEYPAD") {
      if (input == kamar[i].roomPin) {
        idx = i; aksesDiterima = true; tipe = AKSES_KURIR; break;
      }
      if (input == kamar[i].otpAktif && input != "" && kamar[i].statusIsi != LOKER_KOSONG) {
        idx = i; aksesDiterima = true; tipe = AKSES_PENGHUNI;
        kamar[i].otpAktif = "";
        prefs.putString(("otp" + String(i)).c_str(), "");
        break;
      }
    } 
    else if (type == "RFID") {
      if (input == kamar[i].uid) {
        idx = i; aksesDiterima = true; tipe = AKSES_PENGHUNI; break;
      }
    }
  }

  if (!aksesDiterima) {
    showLcdError("Akses Ditolak!", "ID/PIN Salah");
    return;
  }

  if (tipe == AKSES_KURIR && kamar[idx].statusIsi == LOKER_PENUH) {
    DBGF("[Access] Kurir ditolak kamar %d, loker PENUH.\n", idx+1);
    showLcdError("Loker Penuh!", "Hubungi Penghuni");
    kirimPesan(kamar[idx].chatID, "⚠️ *Pemberitahuan:* Ada kurir mencoba mengakses Loker " + String(idx+1) + " namun ditolak karena loker sedang *PENUH*.\nSilakan ambil paket Anda terlebih dahulu agar loker bisa digunakan kembali.");
    return; 
  }

  DBGF("[Access] Diterima kamar %d tipe %s\n", idx+1, tipe == AKSES_KURIR ? "KURIR" : "PENGHUNI");
  tipeAkses[idx] = tipe;
  mulaiAlurKamera(idx, tipe);
}

void mulaiAlurKamera(int idx, TipeAkses tipe) {
  lcd.clear();
  lcd.print("Bersiap foto...");
  lcd.setCursor(0, 1); lcd.print("Hadap ke kamera!");
  delay(FOTO_POSE_DELAY);

  String cmd = "PHOTO:" + String(idx) + ":" + kamar[idx].chatID + "\n";
  Serial2.print(cmd);
  DBGF("[CAM] Kirim: %s", cmd.c_str());

  lcd.clear();
  lcd.print("Mengirim foto...");
  lcd.setCursor(0, 1); lcd.print("Mohon tunggu.");

  lockerPhase[idx] = FASE_TUNGGU_KAMERA;
  timerFase[idx] = millis();
  camBuffer = "";
}

void kirimKonfirmasiKeyboard(int idx, bool fotoSukses) {
  String pesan;
  if (fotoSukses) {
    pesan = "Ada kurir di depan loker kamar " + String(idx+1) + ".\nFoto kurir sudah dikirim di atas.\n\nApakah Anda mengizinkan pintu dibuka?";
  } else {
    pesan = "Ada kurir di depan loker kamar " + String(idx+1) + ".\nFoto kurir akan segera dikirim.\nApakah Anda mengizinkan pintu dibuka?";
  }
  
  String keyboard = "[[{\"text\":\"Terima\",\"callback_data\":\"TERIMA:" + String(idx) + "\"},{\"text\":\"Tolak\",\"callback_data\":\"TOLAK:" + String(idx) + "\"}]]";
  kirimPesan(kamar[idx].chatID, pesan, keyboard);
  DBGF("[TG] Keyboard konfirmasi dikirim ke kamar %d\n", idx+1);
}

bool isDoorClosed(int idx) {
  bool a = (pcf.digitalRead(mc38Pins[idx]) == LOW);
  delay(30);
  bool b = (pcf.digitalRead(mc38Pins[idx]) == LOW);
  return (a && b);
}

int bacaJarak(int idx) {
  VL53L0X_RangingMeasurementData_t m;
  long totalJarak = 0;
  int validReads = 0;
  int failCount = 0;
  const int JUMLAH_SAMPEL = 5; 

  memset(&m, 0xFF, sizeof(m)); 
  vl53[idx].rangingTest(&m, false);
  
  for (int i = 0; i < JUMLAH_SAMPEL; i++) {
    memset(&m, 0xFF, sizeof(m)); 
    vl53[idx].rangingTest(&m, false);
    
    if (m.RangeStatus == 0xFF) {
      failCount++; 
    } else if (m.RangeStatus != 4) { 
      totalJarak += m.RangeMilliMeter;
      validReads++;
    }
  }

  if (failCount >= 3) return -1; 
  if (validReads == 0) return 9999;
  return (int)(totalJarak / validReads);
}

void kirimPesan(String chatID, String pesan, String keyboard) {
  PesanTelegram* p = new PesanTelegram{chatID, pesan, keyboard};
  if (xQueueSend(qPesanTelegram, &p, pdMS_TO_TICKS(100)) != pdTRUE) {
    DBG("[TG] Gagal mengirim ke queue, antrean penuh?");
    delete p;
  }
}

void taskTelegramSender(void* param) {
  PesanTelegram* p;
  while (true) {
    if (xQueueReceive(qPesanTelegram, &p, portMAX_DELAY)) {
      if (xSemaphoreTake(xSendMutex, pdMS_TO_TICKS(20000)) == pdTRUE) {
        DBGF("[TG Send] Mengirim ke %s...\n", p->chatID.c_str());
        String parseMode = "";
        if (p->pesan.indexOf('*') != -1 || p->pesan.indexOf('_') != -1 || p->pesan.indexOf('`') != -1) {
          parseMode = "Markdown";
        }
        if (p->keyboard.length() > 0) {
          botSend.sendMessageWithInlineKeyboard(p->chatID, p->pesan, parseMode, p->keyboard);
        } else {
          botSend.sendMessage(p->chatID, p->pesan, parseMode);
        }
        xSemaphoreGive(xSendMutex);
      } else {
        DBG("[TG Send] Gagal mengambil mutex pengirim.");
      }
      delete p;
    }
  }
}

void taskPollingTelegram(void* param) {
  const TickType_t interval = pdMS_TO_TICKS(2000);
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    vTaskDelayUntil(&lastWake, interval);
    if (WiFi.status() != WL_CONNECTED) continue;
    if (DEBUG_MODE) Serial.print(".");

    int n = botPoll.getUpdates(botPoll.last_message_received + 1);

    if (n > 0) {
      Serial.println();
      DBGF("[TG Poll] %d pesan baru\n", n);
    }

    for (int m = 0; m < n; m++) {
      String type = botPoll.messages[m].type;
      String fromID = botPoll.messages[m].chat_id;

      if (type == "callback_query" && fromID == "") {
        fromID = botPoll.messages[m].from_id;
      }
      
      String text = botPoll.messages[m].text;

      int roomIdx = -1;
      for (int i = 0; i < JUMLAH_LOKER; i++) {
        if (fromID == kamar[i].chatID) { roomIdx = i; break; }
      }

      if (type == "callback_query") {
        String queryId = botPoll.messages[m].query_id;
        Serial.println("[TG] Tombol ditekan: " + text);
        
        if (queryId.length() > 0) {
          botPoll.answerCallbackQuery(queryId, "OK", false);
          DBGF("[TG] answerCallbackQuery: %s\n", queryId.c_str());
        }

        if (roomIdx == -1) {
          DBGF("[TG] Callback dari Chat ID tidak dikenal: %s\n", fromID.c_str());
          continue;
        }

        if (text.startsWith("TERIMA:")) {
          int idx = text.substring(7).toInt();
          if (idx >= 0 && idx < JUMLAH_LOKER && fromID == kamar[idx].chatID && lockerPhase[idx] == FASE_TUNGGU_KONFIRMASI) {
            flagKonfirmasiDiterima[idx] = true;
            Serial.printf("[TG] Loker %d DITERIMA\n", idx+1);
          }
        } else if (text.startsWith("TOLAK:")) {
          int idx = text.substring(6).toInt();
          if (idx >= 0 && idx < JUMLAH_LOKER && fromID == kamar[idx].chatID && lockerPhase[idx] == FASE_TUNGGU_KONFIRMASI) {
            flagKonfirmasiDitolak[idx] = true;
            Serial.printf("[TG] Loker %d DITOLAK\n", idx+1);
          }
        }
        continue;
      }

      if (roomIdx == -1) {
        DBGF("[TG Poll] Chat ID tidak dikenal: %s\n", fromID.c_str());
        continue;
      }

      text.toLowerCase();
      text.trim();
      DBGF("[TG Poll] Kamar %d: %s\n", roomIdx+1, text.c_str());

      if (text == "/reset") {
        flagReset[roomIdx] = true;
      } else if (text == "/status") {
        flagStatus[roomIdx] = true;
      } else if (text == "/start") {
        kirimPesan(fromID, "/status - Cek status loker Anda\n/reset - Paksa loker ke mode standby jika sistem error/nyangkut");
      }
    }
  }
}

void initVL53() {
  DBG("[VL53] Inisialisasi...");
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    pcf.digitalWrite(xshutPins[i], HIGH);
    delay(50);
    if (!vl53[i].begin(vl53Addr[i], false, &Wire)) {
      DBGF("[ERROR] VL53L0X loker %d gagal!\n", i+1);
      lcd.clear(); lcd.print("ERR VL53 L" + String(i+1));
      delay(2000);
    } else {
      vl53[i].setMeasurementTimingBudgetMicroSeconds(200000);
      DBGF("[VL53] Loker %d OK -> 0x%02X\n", i+1, vl53Addr[i]);
    }
    delay(0);
  }
  DBG("[VL53] Semua siap.");
}

String readUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uid += String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.trim(); uid.toUpperCase();
  return uid;
}

void updateLoker(int idx, StatusIsiLoker status, String otp) {
  kamar[idx].statusIsi = status;
  kamar[idx].otpAktif = otp;
  prefs.putUChar(("st" + String(idx)).c_str(), (uint8_t)status);
  prefs.putString(("otp" + String(idx)).c_str(), otp);
}

void wakeUpLcd() {
  waktuAktivitasTerakhir = millis();
  if (isLcdAsleep) {
    isLcdAsleep = false;
    lcd.backlight();
    showIdleMessage();
  }
}

void handleLcdSleep() {
  if (isLcdAsleep) return;
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    if (lockerPhase[i] != FASE_IDLE) {
      waktuAktivitasTerakhir = millis(); 
      return;
    }
  }
  if (millis() - waktuAktivitasTerakhir > LCD_TIMEOUT) {
    isLcdAsleep = true;
    lcd.clear();
    lcd.noBacklight();
  }
}

void showIdleMessage() {
  if (isLcdAsleep) return; 
  for (int i = 0; i < JUMLAH_LOKER; i++)
    if (lockerPhase[i] != FASE_IDLE) return;
  lcd.clear();
  lcd.setCursor(0, 0); 
  lcd.print("SIAP TERIMA  ");
  lcd.print(WiFi.status() == WL_CONNECTED ? "[W]" : "[X]");
  lcd.setCursor(0, 1);
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    char statusChar = 'K'; 
    if (kamar[i].statusIsi == LOKER_TERISI_SEBAGIAN) statusChar = 'S';
    else if (kamar[i].statusIsi == LOKER_PENUH) statusChar = 'P';
    lcd.print(String(i+1) + ":" + statusChar + " ");
  }
}

void showLcdError(String line1, String line2) {
  wakeUpLcd();
  lcd.clear();
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  waktuTolak = millis();
  sedangTolak = true;
}

void initDatabase() {
  if (!prefs.getBool("isInit", false)) {
    DBG("[DB] Inisialisasi data awal.");
    prefs.putString("wifi_ssid", "");
    prefs.putString("wifi_pass", "");
    prefs.putString("admin_pin", ADMIN_PIN_DEFAULT);
    prefs.putString("pin0", "01"); prefs.putString("uid0", "00 00 00 00"); prefs.putString("cid0", "000000000");
    prefs.putString("pin1", "02"); prefs.putString("uid1", "11 11 11 11"); prefs.putString("cid1", "111111111");
    prefs.putString("pin2", "03"); prefs.putString("uid2", "22 22 22 22"); prefs.putString("cid2", "222222222");
    for (int i = 0; i < JUMLAH_LOKER; i++) {
      prefs.putUChar(("st" + String(i)).c_str(), LOKER_KOSONG);
      prefs.putString(("otp" + String(i)).c_str(), "");
      prefs.putBool(("kb" + String(i)).c_str(), true);
    }
    prefs.putBool("isInit", true);
  }

  for (int i = 0; i < JUMLAH_LOKER; i++) {
    kamar[i].roomPin = prefs.getString(("pin" + String(i)).c_str(), "0" + String(i+1));
    kamar[i].uid = prefs.getString(("uid" + String(i)).c_str(), "");
    kamar[i].chatID = prefs.getString(("cid" + String(i)).c_str(), "");
    kamar[i].statusIsi = (StatusIsiLoker)prefs.getUChar(("st" + String(i)).c_str(), LOKER_KOSONG);
    kamar[i].otpAktif = prefs.getString(("otp" + String(i)).c_str(), "");
    kamar[i].kurirBolehMasuk = prefs.getBool(("kb" + String(i)).c_str(), true);

    String statusIsiStr = "KOSONG";
    if (kamar[i].statusIsi == LOKER_TERISI_SEBAGIAN) statusIsiStr = "SEBAGIAN";
    else if (kamar[i].statusIsi == LOKER_PENUH) statusIsiStr = "PENUH";
    DBGF("[DB] Kamar %d | PIN:%s | Isi:%s | KurirBoleh:%s\n", i+1, kamar[i].roomPin.c_str(), statusIsiStr.c_str(), kamar[i].kurirBolehMasuk ? "YA" : "TIDAK");
  }
  adminPin = prefs.getString("admin_pin", ADMIN_PIN_DEFAULT);
  masterUid = prefs.getString("master_uid", "");
}

void triggerAdminMode() {
  adminState = ADMIN_WAIT_PIN; adminInputBuf = "";
  lcd.clear(); lcd.print("Admin: PIN?");
  lcd.setCursor(0, 1); lcd.print("Input: ");
}

void exitAdminMode() {
  adminState = ADMIN_NONE; adminTargetRoom = -1; adminInputBuf = "";
  showIdleMessage();
}

void showAdminMenu() {
  adminState = ADMIN_MENU;
  lcd.clear(); lcd.print("1KTP 2TG 3Wi 4Rs");
  lcd.setCursor(0, 1); lcd.print("5PIN 6Mstr *Exit");
}

void showAdminSelectRoomMenu(String title) {
  lcd.clear();
  lcd.print(title);
  lcd.setCursor(0, 1);
  lcd.print("1  2  3   *Batal");
}

void handleAdminKey(char key) {
  switch (adminState) {
    case ADMIN_WAIT_PIN:
      if (key == '*') { exitAdminMode(); return; }
      if (key == '#') {
        if (adminInputBuf == adminPin) { adminInputBuf = ""; showAdminMenu(); }
        else { lcd.clear(); lcd.print("PIN Salah!"); delay(DURASI_PESAN_LCD); exitAdminMode(); }
        return;
      }
      if (key == 'B') {
        if (adminInputBuf.length() > 0) {
          adminInputBuf.remove(adminInputBuf.length() - 1);
          lcd.setCursor(7, 1); lcd.print("         ");
          lcd.setCursor(7, 1);
          for (int i = 0; i < (int)adminInputBuf.length(); i++) lcd.print("*");
        }
      } else if (key == 'C') {
        adminInputBuf = ""; lcd.setCursor(7, 1); lcd.print("         ");
      } else if (key != 'A' && key != 'D') {
        if ((int)adminInputBuf.length() < MAX_PIN_LENGTH) {
          adminInputBuf += key;
          lcd.setCursor(7, 1); lcd.print("         ");
          lcd.setCursor(7, 1); 
          for (int i = 0; i < (int)adminInputBuf.length(); i++) lcd.print("*");
        }
      }
      break;

    case ADMIN_MENU:
      if (key == '*') { exitAdminMode(); return; }
      if (key == '1') { adminState = ADMIN_SELECT_ROOM_RFID; showAdminSelectRoomMenu("Pilih Kamar KTP:"); }
      else if (key == '2') { adminState = ADMIN_SELECT_ROOM_TG; showAdminSelectRoomMenu("Ubah TG ID Kmr:"); }
      else if (key == '3') { startWifiPortal(); }
      else if (key == '4') { adminState = ADMIN_SELECT_ROOM_RESET; showAdminSelectRoomMenu("Reset Kamar:"); }
      else if (key == '5') { adminState = ADMIN_CHANGE_PIN; adminInputBuf = ""; lcd.clear(); lcd.print("PIN Admin Baru:"); lcd.setCursor(0, 1); }
      else if (key == '6') { adminState = ADMIN_SET_MASTER_CARD; lcd.clear(); lcd.print("Tempel Kartu Utk"); lcd.setCursor(0, 1); lcd.print("Master Admin..."); }
      break;

    case ADMIN_SELECT_ROOM_RFID:
    case ADMIN_SELECT_ROOM_TG:
    case ADMIN_SELECT_ROOM_RESET:
      if (key == '*') { showAdminMenu(); return; }
      if (key >= '1' && key <= '3') {
        adminTargetRoom = key - '1';
        if (adminState == ADMIN_SELECT_ROOM_RFID) {
          adminState = ADMIN_SCAN_CARD;
          lcd.clear(); lcd.print("Tempel kartu/KTP"); lcd.setCursor(0, 1); lcd.print("-> Kamar " + String(adminTargetRoom+1));
        } else if (adminState == ADMIN_SELECT_ROOM_TG) {
          adminState = ADMIN_INPUT_TG;
          adminInputBuf = "";
          lcd.clear(); lcd.print("Input Chat ID:"); lcd.setCursor(0, 1);
          String old = kamar[adminTargetRoom].chatID;
          lcd.print(old.length() > 14 ? "..." + old.substring(old.length()-11) : old);
        } else if (adminState == ADMIN_SELECT_ROOM_RESET) {
          adminState = ADMIN_CONFIRM_RESET;
          lcd.clear(); lcd.print("Reset Kamar " + String(adminTargetRoom+1) + "?"); lcd.setCursor(0, 1); lcd.print("# Ya     * Tidak");
        }
      }
      break;

    case ADMIN_SCAN_CARD:
      if (key == '*') showAdminMenu();
      break;
      
    case ADMIN_INPUT_TG:
      if (key == '*') { showAdminMenu(); return; }
      if (key == '#') {
        if (adminInputBuf.length() > 0) {
          kamar[adminTargetRoom].chatID = adminInputBuf;
          prefs.putString(("cid" + String(adminTargetRoom)).c_str(), adminInputBuf);
          lcd.clear(); lcd.print("Tersimpan!"); lcd.setCursor(0, 1); lcd.print("Kamar " + String(adminTargetRoom+1));
          delay(DURASI_PESAN_LCD);
        }
        adminInputBuf = ""; showAdminMenu();
      } else if (key == 'B') {
        if (adminInputBuf.length() > 0) {
          adminInputBuf.remove(adminInputBuf.length() - 1);
          lcd.setCursor(0, 1); lcd.print("               "); lcd.setCursor(0, 1); lcd.print(adminInputBuf);
        }
      } else if (key == 'C') {
        adminInputBuf = ""; lcd.setCursor(0, 1); lcd.print("               ");
      } else if (key != 'A' && key != 'D') {
        if ((int)adminInputBuf.length() < 15) {
          adminInputBuf += key;
          lcd.setCursor(0, 1); lcd.print("               "); lcd.setCursor(0, 1); lcd.print(adminInputBuf);
        }
      }
      break;

    case ADMIN_CONFIRM_RESET:
      if (key == '*') { showAdminMenu(); return; }
      if (key == '#') {
        updateLoker(adminTargetRoom, LOKER_KOSONG, "");
        kamar[adminTargetRoom].kurirBolehMasuk = true;
        prefs.putBool(("kb" + String(adminTargetRoom)).c_str(), true);
        lockerPhase[adminTargetRoom] = FASE_IDLE;
        lcd.clear(); lcd.print("Berhasil Reset!"); lcd.setCursor(0, 1); lcd.print("Kamar " + String(adminTargetRoom+1) + " = Kosong");
        delay(DURASI_PESAN_LCD);
        showAdminMenu();
      }
      break;

    case ADMIN_CHANGE_PIN:
      if (key == '*') { showAdminMenu(); return; }
      if (key == '#') {
        if (adminInputBuf.length() >= 4) {
          adminPin = adminInputBuf;
          prefs.putString("admin_pin", adminPin);
          lcd.clear(); lcd.print("PIN Tersimpan!"); delay(2000);
        } else {
          lcd.clear(); lcd.print("Min 4 Digit!"); delay(2000);
        }
        adminInputBuf = ""; showAdminMenu();
      } else if (key == 'B') {
        if (adminInputBuf.length() > 0) {
          adminInputBuf.remove(adminInputBuf.length() - 1);
          lcd.setCursor(0, 1); lcd.print("                "); lcd.setCursor(0, 1); 
          for(int i=0; i<(int)adminInputBuf.length(); i++) lcd.print("*");
        }
      } else if (key == 'C') {
        adminInputBuf = ""; lcd.setCursor(0, 1); lcd.print("                ");
      } else if (key != 'A' && key != 'D') {
        if ((int)adminInputBuf.length() < MAX_PIN_LENGTH) {
          adminInputBuf += key;
          lcd.setCursor(0, 1); lcd.print("                "); lcd.setCursor(0, 1);
          for(int i=0; i<(int)adminInputBuf.length(); i++) lcd.print("*");
        }
      }
      break;

    default: break;
  }
}

void adminRegisterCard(String uid) {
  if (adminState == ADMIN_SET_MASTER_CARD) {
    masterUid = uid;
    prefs.putString("master_uid", uid);
    lcd.clear(); lcd.print("Master Disimpan!");
    lcd.setCursor(0, 1); lcd.print(uid.length() > 14 ? uid.substring(0,11)+"..." : uid);
    delay(2500);
    showAdminMenu();
    return;
  }

  if (adminTargetRoom < 0 || adminTargetRoom >= JUMLAH_LOKER) {
    return;
  }
  kamar[adminTargetRoom].uid = uid;
  prefs.putString(("uid" + String(adminTargetRoom)).c_str(), uid);
  lcd.clear(); lcd.print("UID Tersimpan!"); lcd.setCursor(0, 1);
  lcd.print(uid.length() > 14 ? uid.substring(0,11)+"..." : uid);
  delay(2500);
  showAdminMenu();
}

void startWifiPortal() {
  lcd.clear(); lcd.print("Scan WiFi...");
  WiFi.disconnect();
  delay(300);
  WiFi.softAP("LokerConfig", "", 1, false, 4); 

  int n = WiFi.scanNetworks();
  String opts = "";
  if (n == 0) opts = "<option disabled>Tidak ada jaringan</option>";
  else for (int i = 0; i < n; i++)
    opts += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";

  lcd.clear(); lcd.print("WiFi Config");
  lcd.setCursor(0, 1); lcd.print("SSID:LokerConfig");

  WebServer server(80);
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Config WiFi</title><style>body{font-family:sans-serif; max-width:380px; margin:32px auto; padding:0 16px}select,input{display:block; width:100%; box-sizing:border-box; padding:10px; margin-top:6px; border:1px solid #ccc; border-radius:6px; font-size:15px}button{display:block; width:100%; margin-top:16px; padding:12px; background:#1976D2; color:#fff; border:none; border-radius:6px; font-size:16px; cursor:pointer}</style></head><body><h2>Pilih Jaringan WiFi</h2><form method='POST' action='/save'><label>Jaringan:<select name='ssid'><option value='' disabled selected>-- Pilih --</option>" + opts + "</select></label><label style='margin-top:14px; display:block'>Password:<input name='pass' type='password' placeholder='Kosongkan jika terbuka'></label><button type='submit'>Simpan &amp; Restart</button></form></body></html>";

  server.on("/", HTTP_GET, [&]() { server.send(200, "text/html; charset=utf-8", html); });
  server.on("/save", HTTP_POST, [&]() {
    String ns = server.arg("ssid");
    String np = server.arg("pass");
    if (ns.length() == 0) { server.send(400, "text/html", "<p>SSID kosong.</p>"); return; }

    prefs.putString("wifi_ssid", ns);
    prefs.putString("wifi_pass", np);

    String wifiCmd = "WIFI:" + ns + ":" + np + "\n";
    Serial2.print(wifiCmd);
    delay(500);

    server.send(200, "text/html", "<h2>Tersimpan! Restarting...</h2>");
    lcd.clear(); lcd.print("WiFi Disimpan!"); lcd.setCursor(0,1); lcd.print("Restart...");
    delay(3000); ESP.restart();
  });

  server.begin();
  unsigned long t = millis();
  while (millis() - t < PORTAL_TIMEOUT_MS) {
    server.handleClient();
    if (keypad.getKey() == '*') break;
    delay(5);
  }
  server.stop();
  WiFi.softAPdisconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA); 
  WiFi.setAutoReconnect(true);
  String s = prefs.getString("wifi_ssid", "");
  String p = prefs.getString("wifi_pass", "");
  if (s.length() > 0) WiFi.begin(s.c_str(), p.c_str());
  delay(1000);
  exitAdminMode();
}

void checkI2CHealth() {
  static unsigned long lastI2CCheck = 0;
  
  if (millis() - lastI2CCheck > 3000) {
    lastI2CCheck = millis();
    Wire.beginTransmission(0x20); 
    if (Wire.endTransmission() != 0) {
      DBGF("[I2C] Bus Hang terdeteksi secara background! Recovery...\n");
      recoverI2CAndSensors();
    }
  }
}

void clearI2CBus() {
  pinMode(21, INPUT_PULLUP);
  pinMode(22, INPUT_PULLUP);
  delay(5);
   if (digitalRead(21) == LOW) {
    pinMode(22, OUTPUT);
    for (int i = 0; i < 16; i++) {
      digitalWrite(22, LOW);
      delayMicroseconds(10);
      digitalWrite(22, HIGH);
      delayMicroseconds(10);
    }
    pinMode(22, INPUT_PULLUP);
  }
}

void recoverI2CAndSensors() {
  lcd.clear(); lcd.print("Sistem Koreksi..");
  lcd.setCursor(0, 1); lcd.print("Tunggu sebentar.");
  
  Wire.end();
  clearI2CBus();
  delay(10);
  Wire.begin(21, 22);
  Wire.setClock(50000);
  Wire.setTimeOut(150);

  pcf.begin(0x20, &Wire);
  
  for (int i = 0; i < JUMLAH_LOKER; i++) {
    pcf.pinMode(xshutPins[i], OUTPUT);
    pcf.pinMode(mc38Pins[i], INPUT_PULLUP);
    pcf.digitalWrite(xshutPins[i], LOW);
  }
  delay(100);
  initVL53(); 
  
  showIdleMessage();
}

