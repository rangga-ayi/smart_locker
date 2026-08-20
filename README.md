# Smart Locker Paket Berbasis ESP32 Menggunakan FreeRTOS dengan Autentikasi RFID dan OTP Telegram

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Bot](https://img.shields.io/badge/Integration-Telegram%20Bot%20API-blue.svg)](https://core.telegram.org/bots)
[![Institution](https://img.shields.io/badge/Academic-Politeknik%20Negeri%20Sriwijaya-red.svg)](https://www.polsri.ac.id/)
[![HKI](https://img.shields.io/badge/Hak%20Cipta-DJKI%20Kemenkumham%20RI-purple.svg)](https://dgip.go.id/)

> **Laporan Tugas Akhir D3 Teknik Komputer — Politeknik Negeri Sriwijaya (2026)**  
> **Penulis / Pencipta Utama:** Rangga Ayi Pratama (NIM: 062330701526)

---

## Ringkasan Proyek

Peningkatan belanja online di area indekos sering kali terkendala karena penghuni tidak berada di tempat saat kurir tiba. Penelitian ini mengembangkan prototipe **Smart Locker 3 Kompartemen** mandiri (_standalone_) berbasis IoT dengan sistem operasi real-time **FreeRTOS** pada mikrokontroler **ESP32**, dipadukan dengan **ESP32-CAM** untuk dokumentasi foto wajah kurir.

Sistem mengatasi permasalahan umum loker pintar generasi sebelumnya:

1. **Bebas Macet (_Zero System Blocking_):** FreeRTOS membagi beban kerja ke dua inti CPU ESP32 secara asinkron (Core 0 khusus menangani jaringan/Telegram, Core 1 khusus menangani hardware lokal).
2. **Keamanan Berlapis:** Autentikasi ganda via kartu **e-KTP (RFID RC522)** dan kode **4-Digit OTP Telegram (_auto-burn_)**.
3. **Mandiri & Tangguh (_Offline-Ready_):** Seluruh basis data penghuni (UID e-KTP, Telegram Chat ID, PIN Admin) disimpan di **Non-Volatile Storage (NVS)** internal ESP32 tanpa ketergantungan pada cloud/database eksternal.

---

## Dokumentasi Perangkat Fisik & Antarmuka

|       Dokumentasi Alat Fisik Smart Locker        |                Tampilan Antarmuka Bot Telegram                |
| :----------------------------------------------: | :-----------------------------------------------------------: |
| ![Foto Alat Smart Locker](images/Foto_Alat.avif) | ![Antarmuka Bot Telegram](images/Antarmuka_Bot_Telegram.avif) |
| _Implementasi Fisik Smart Locker 3 Kompartemen_  |      _Notifikasi Real-time & Izin Akses OTP di Telegram_      |

---

## Arsitektur Sistem

Sistem Smart Locker ini dibangun menggunakan **dua mikrokontroler** yang saling berkomunikasi:

### ESP32 Utama — Dual-Core FreeRTOS

| Inti Prosesor | Tugas Utama                | Detail                                                                                                                           |
| :-----------: | -------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
|  **Core 0**   | Jaringan & Bot Telegram    | Polling pesan Telegram, kirim notifikasi & foto kurir, generate OTP, manajemen koneksi WiFi                                      |
|  **Core 1**   | Perangkat Keras & UI Lokal | Pemindaian RFID RC522 (e-KTP), pembacaan keypad 4x4, kontrol relay solenoid, tampilan LCD 16x2, pembacaan sensor VL53L0X & MC-38 |

> Pemisahan ini memastikan **sistem tidak pernah macet (_zero blocking_)** — meskipun koneksi internet lambat atau proses upload foto sedang berlangsung, keypad dan RFID tetap responsif.

### ESP32-CAM — Kamera Dokumentasi

- Terhubung ke ESP32 Utama via **komunikasi Serial UART** (TX0/RX0).
- Mengambil foto wajah kurir saat proses drop-off dan mengirimkannya ke Bot Telegram untuk verifikasi oleh penghuni.

### Jalur Komunikasi Perangkat

| Protokol          | Komponen                       | Keterangan                                                                                               |
| ----------------- | ------------------------------ | -------------------------------------------------------------------------------------------------------- |
| **SPI**           | RFID RC522                     | Pemindaian UID kartu e-KTP (GPIO 5, 18, 19, 23, 2)                                                       |
| **I2C (50 kHz)**  | LCD 16x2, 3x VL53L0X, PCF8574  | Bus bersama pada GPIO 21 (SDA) & 22 (SCL). Frekuensi diturunkan ke 50 kHz untuk kestabilan kabel panjang |
| **UART Serial**   | ESP32-CAM                      | Jalur komunikasi foto (GPIO 1 TX → U0R, GPIO 3 RX → U0T)                                                 |
| **GPIO Langsung** | Keypad 4x4, Relay 3-Ch, Buzzer | Kendali langsung tanpa protokol tambahan                                                                 |

### Penyimpanan Data Lokal (Offline-Ready)

Seluruh data penting disimpan di **Non-Volatile Storage (NVS)** internal ESP32 menggunakan library `Preferences`:

- UID e-KTP terdaftar per kamar
- Telegram Chat ID penghuni
- PIN Admin & Master Card
- Status kompartemen loker

> Sistem tetap bisa membuka loker via e-KTP **meskipun internet terputus**.

---

## Alur Operasional Sistem

### 1. Flowchart Alur Sistem Utama (Drop-off & Pengambilan Paket)

Diagram berikut menjelaskan alur lengkap mulai dari inisialisasi perangkat, proses kurir menitipkan paket (input nomor loker → foto → OTP → buka pintu), hingga proses penghuni mengambil paket (tempel e-KTP / input OTP → buka pintu → update status):

![Flowchart Alur Sistem Utama](schematics/Alur%20Sistem%20Utama.avif)

_Flowchart Alur Sistem Utama — Proses Drop-off Kurir (kiri) & Pengambilan Penghuni (kanan)_

### 2. Flowchart Alur Sistem Mode Admin

Diagram berikut menjelaskan alur akses Mode Admin melalui tombol **`A`** pada keypad, yang memiliki 6 opsi konfigurasi mandiri:

![Flowchart Alur Sistem Mode Admin](schematics/Alur%20Sistem%20Mode%20Admin.avif)

_Flowchart Mode Admin — Menu Konfigurasi Lokal (1: RFID, 2: Telegram ID, 3: WiFi, 4: Reset, 5: PIN, 6: Master Card)_

---

## Skema Rangkaian & Pengkabelan (Schematics)

### 1. Wiring Diagram Sistem

![Wiring Diagram](schematics/Wiring_Diagram.avif)
_Diagram Pengkabelan Komponen Loker_

### 2. Skematik Rangkaian Detail

![Skematik Rangkaian](schematics/skematik_rangkaian.avif)
_Skematik Koneksi Pinout & Jalur Daya_

---

## Rincian Perangkat Keras (Hardware Specs)

| Komponen                 | Spesifikasi / Model                     | Peran & Konfigurasi Pin                                             |
| ------------------------ | --------------------------------------- | ------------------------------------------------------------------- |
| **Mikrokontroler Utama** | ESP32 DevKit V1 (30 Pin)                | Otak pemrosesan FreeRTOS dual-core                                  |
| **Kamera Pengawas**      | ESP32-CAM + OV2640                      | Dokumentasi foto kurir via Serial UART (GPIO 1 TX, GPIO 3 RX)       |
| **Sensor Jarak Paket**   | 3x VL53L0X (Laser Time-of-Flight)       | Deteksi kapasitas isi kompartemen loker via I2C                     |
| **Sensor Status Pintu**  | 3x Magnetic Switch MC-38                | Deteksi fisik pintu terbuka/tertutup                                |
| **I/O Expander**         | PCF8574 Module                          | Kontrol pin XSHUT sensor ToF (P0, P2, P4) & baca MC-38 (P1, P3, P5) |
| **Pembaca Kartu**        | RFID RC522 (13.56 MHz)                  | Pemindaian e-KTP penghuni via bus SPI (GPIO 5, 18, 19, 23, 2)       |
| **Input Manual**         | Keypad Matrix 4x4                       | Input OTP dan navigasi Menu Admin lokal (8 GPIO)                    |
| **Antarmuka Display**    | LCD 16x2 Karakter + I2C Backpack        | Tampilan panduan pengguna & konfigurasi admin (SDA 21, SCL 22)      |
| **Kunci Pintu**          | 3x Solenoid Door Lock 12V               | Pengunci mekanik pintu kompartemen                                  |
| **Driver Aktuator**      | Relay Module 3-Channel 5V (Optocoupler) | Saklar elektromagnetik kendali Solenoid (GPIO 4, 16, 17)            |
| **Indikator Audio**      | Active Buzzer 5V                        | Nada konfirmasi berhasil/gagal input                                |
| **Catu Daya Utama**      | Power Supply Switching 12V 5A DC        | Sumber daya solenoid & sistem                                       |
| **Regulator Tegangan**   | Buck Converter LM2596 (Step-Down 5V)    | Penurun tegangan 12V ke 5V stabil untuk ESP32 dan periferal         |
| **Material Bodi**        | Plywood Kayu 15mm                       | Bodi non-logam untuk menjaga kinerja transmisi sinyal RFID          |

---

## Arsitektur Perangkat Lunak & FreeRTOS

Pengembangan perangkat lunak dibangun di lingkungan **Arduino IDE (C/C++)** dengan pemanfaatan **FreeRTOS kernel**:

### 1. Pembagian Task Dual-Core:

- **Task Core 0 (`TaskTelegram` & `TaskNetwork`):**
  - Prioritas menengah-rendah, siklus polling pesan Telegram asinkron.
  - Menangani transmisi HTTP multipart saat mengirim foto dari ESP32-CAM ke Bot Telegram.
  - Manajemen timer kedaluwarsa kode OTP.
- **Task Core 1 (`TaskHardware` & `TaskUI`):**
  - Prioritas tinggi real-time.
  - Polling pemindaian RFID RC522 dan pembacaan matriks keypad tanpa delay.
  - Pengukuran jarak laser VL53L0X secara berkala dan pembacaan saklar pintu MC-38.
  - Kontrol relay solenoid dan rendering tampilan LCD 16x2.

### 2. Mode Admin Mandiri (_Local Configuration_):

- Pengelola indekos dapat menekan tombol **`A`** pada keypad untuk masuk ke menu admin.
- Terproteksi **Master Key RFID** atau **PIN Admin**.
- Fitur admin lokal (langsung via LCD & Keypad tanpa komputer):
  1. _Register New e-KTP / Penghuni Baru_ (simpan UID ke NVS).
  2. _Update Chat ID Telegram Penghuni_.
  3. _Manual Unlock & Reset Status Kompartemen_.
  4. _Change Master PIN / Master Card_.

---

## Hasil Pengujian & Performa

Berdasarkan pengujian teknis pada laporan Tugas Akhir:

- **Tingkat Keberhasilan Skenario:** **100%** sukses pada 14 skenario pengujian autentikasi (RFID, e-KTP, Keypad PIN, OTP Telegram, dan Mode Admin).
- **Stabilitas Sistem (FreeRTOS):** Nol kasus _system freeze_ atau _blocking_ saat koneksi internet lambat / proses upload foto berlangsung.
- **Stabilitas Catu Daya Listrik:**
  - Output LM2596 (5V): Terukur stabil di **4.95V – 5.02V** (deviasi hanya 0.2% – 1.0%, batas aman < 5%).
  - Jalur Suplai Solenoid (12V): Saat tarikan arus solenoid aktif, tegangan terukur **11.20V – 11.23V** (penurunan deviasi 6.5%, sesuai standar internasional catu daya ATX).
- **Optimasi Bus I2C:** Mengatasi penurunan integritas sinyal akibat panjang kabel dengan menurunkan frekuensi clock I2C ke **50 kHz** dan menerapkan filter perataan data (_averaging filter_).

---

## Struktur Repositori Saat Ini

```text
Smart-Locker/
├── docs/
│   └── SMART LOCKER PAKET.pdf          # Dokumen resmi pendaftaran HKI & spesifikasi sistem
├── firmware/
│   ├── smart_locker_main/
│   │   └── smart_locker_main.ino       # Firmware ESP32 Utama (FreeRTOS & logika kendali)
│   └── esp32_cam_slave/
│       └── esp32_cam_slave.ino         # Firmware ESP32-CAM (Capture & Serial Bridge)
├── images/
│   ├── Foto_Alat.avif                   # Foto fisik prototipe Smart Locker 3 pintu
│   └── Antarmuka_Bot_Telegram.avif      # Tampilan interaksi notifikasi & tombol bot Telegram
├── schematics/
│   ├── Alur Sistem Utama.avif           # Flowchart alur sistem utama (drop-off & pengambilan)
│   ├── Alur Sistem Mode Admin.avif      # Flowchart alur mode admin (6 menu konfigurasi)
│   ├── Wiring_Diagram.avif              # Diagram perkabelan komponen & sensor
│   └── skematik_rangkaian.avif          # Skema jalur rangkaian elektronik
└── README.md
```

---

## Panduan Instalasi & Setup

1. **Persiapan Perangkat Lunak:**
   - Install [Arduino IDE](https://www.arduino.cc/en/software) (v2.x disarankan).
   - Tambahkan board package ESP32: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`.
2. **Instalasi Library yang Dibutuhkan:**
   - `UniversalTelegramBot` (oleh Brian Lough)
   - `ArduinoJson` (v6.x)
   - `MFRC522`
   - `Adafruit_VL53L0X`
   - `Adafruit_PCF8574`
   - `LiquidCrystal_I2C`
   - `Keypad`
3. **Konfigurasi Kredensial:**
   - Buka file `firmware/smart_locker_main/smart_locker_main.ino`, sesuaikan konfigurasi WiFi SSID, Password, dan Token Bot Telegram Anda (pastikan tidak mengunggah token asli ke repositori publik).
4. **Wiring & Upload:**
   - Hubungkan ESP32 dan ESP32-CAM sesuai tabel konfigurasi pin dan diagram skema di folder `schematics/`.
   - Upload firmware ke masing-masing mikrokontroler.

---

## Tim Pencipta & Hak Kekayaan Intelektual (HKI)

Karya perangkat lunak dan arsitektur sistem Smart Locker ini telah terdaftar secara resmi sebagai **Hak Cipta Program Komputer** pada **Direktorat Jenderal Kekayaan Intelektual (DJKI) Kementerian Hukum dan HAM Republik Indonesia**:

- **Pencipta Utama:** Rangga Ayi Pratama (NIM: 062330701526)
- **Co-Pencipta 1 (Dosen Pembimbing):** Ica Admirani, S.Kom., M.Kom. (NIP: 197903282005012001)
- **Co-Pencipta 2 (Dosen Pembimbing):** Della Oktaviany, S.Kom., M.T.I. (NIP: 199010072022032005)
- **Institusi:** Program Studi D3 Teknik Komputer — Politeknik Negeri Sriwijaya (POLSRI), Palembang (2026)
- **Dokumen Terkait:** _Manual Penggunaan dan Spesifikasi Program Komputer — DJKI Kemenkumham RI_ (tersedia di `docs/SMART LOCKER PAKET.pdf`)

---

## Kontak & Portofolio

- **LinkedIn:** [linkedin.com/in/rangga](https://linkedin.com/in/rangga)
- **Portofolio Web:** [rangga.dev](https://rangga.dev)
- **Email:** rangga@example.com
