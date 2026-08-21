# Smart Locker Paket Berbasis ESP32 Menggunakan FreeRTOS dengan Autentikasi RFID dan OTP Telegram

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Bot](https://img.shields.io/badge/Integration-Telegram%20Bot%20API-blue.svg)](https://core.telegram.org/bots)
[![Institution](https://img.shields.io/badge/Academic-Politeknik%20Negeri%20Sriwijaya-red.svg)](https://www.polsri.ac.id/)
[![HKI](https://img.shields.io/badge/Hak%20Cipta-DJKI%20Kemenkumham%20RI-purple.svg)](https://dgip.go.id/)

> **Laporan Tugas Akhir D3 Teknik Komputer, Politeknik Negeri Sriwijaya (2026)**  
> **Penulis / Pencipta Utama:** Rangga Ayi Pratama (NIM: 062330701526)

---

## Ringkasan Proyek

Penghuni indekos sering tidak berada di tempat saat kurir mengantar paket. Proyek ini mengimplementasikan prototipe **Smart Locker 3 Kompartemen** mandiri (_standalone_) berbasis **ESP32** dengan **FreeRTOS** dan modul kamera **ESP32-CAM** untuk dokumentasi visual kurir.

Karakteristik teknis sistem:

1. **Bebas Macet (_Zero System Blocking_):** FreeRTOS memisahkan beban kerja ke dua inti CPU ESP32. Core 0 menangani koneksi WiFi dan bot Telegram; Core 1 mengendalikan sensor, keypad, RFID, dan aktuator lokal.
2. **Autentikasi Ganda:** Pengambilan paket mendukung kartu e-KTP (RFID RC522) dan kode 4-digit OTP Telegram yang hangus sekali pakai (_auto-burn_).
3. **Penyimpanan Lokal (_Offline-Ready_):** Data penghuni (UID e-KTP, Telegram Chat ID, PIN Admin) tersimpan di _Non-Volatile Storage_ (NVS) internal ESP32 tanpa ketergantungan server luar.

---

## Dokumentasi Perangkat Fisik & Antarmuka

|       Dokumentasi Alat Fisik Smart Locker        |                Tampilan Antarmuka Bot Telegram                |
| :----------------------------------------------: | :-----------------------------------------------------------: |
| ![Foto Alat Smart Locker](images/Foto_Alat.avif) | ![Antarmuka Bot Telegram](images/Antarmuka_Bot_Telegram.avif) |
| _Implementasi Fisik Smart Locker 3 Kompartemen_  |      _Notifikasi Real-time & Izin Akses OTP di Telegram_      |

---

## Arsitektur Sistem

Sistem menggunakan dua mikrokontroler yang terhubung via serial UART:

### ESP32 Utama (Dual-Core FreeRTOS)

| Inti Prosesor | Tugas Utama                | Detail                                                                                                                             |
| :-----------: | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
|  **Core 0**   | Jaringan & Bot Telegram    | Polling pesan Telegram, pengiriman notifikasi dan foto kurir, pembuatan OTP, manajemen WiFi                                        |
|  **Core 1**   | Perangkat Keras & UI Lokal | Pemindaian RFID RC522 (e-KTP), pembacaan keypad 4x4, kontrol relay solenoid, tampilan LCD 16x2, pembacaan sensor VL53L0X dan MC-38 |

> Pemisahan task antar-inti menjaga responsivitas input keypad dan RFID saat ESP32 mengunggah foto atau mengalami latensi jaringan.

### ESP32-CAM (Kamera Dokumentasi)

- Terhubung ke ESP32 Utama melalui komunikasi serial UART (TX0/RX0).
- Mengambil foto wajah kurir saat proses drop-off dan mengirimkannya ke bot Telegram penghuni.

### Jalur Komunikasi Perangkat

| Protokol          | Komponen                       | Keterangan                                                                                                 |
| ----------------- | ------------------------------ | ---------------------------------------------------------------------------------------------------------- |
| **SPI**           | RFID RC522                     | Pemindaian UID kartu e-KTP (GPIO 5, 18, 19, 23, 2)                                                         |
| **I2C (50 kHz)**  | LCD 16x2, 3x VL53L0X, PCF8574  | Bus bersama pada GPIO 21 (SDA) dan 22 (SCL). Clock disetel ke 50 kHz untuk kestabilan sinyal kabel panjang |
| **UART Serial**   | ESP32-CAM                      | Jalur transfer data foto (GPIO 1 TX ke U0R, GPIO 3 RX ke U0T)                                              |
| **GPIO Langsung** | Keypad 4x4, Relay 3-Ch, Buzzer | Kendali I/O langsung tanpa protokol serial                                                                 |

### Penyimpanan Data Lokal (Offline-Ready)

Data penting tersimpan di Non-Volatile Storage (NVS) ESP32 melalui pustaka `Preferences`:

- UID e-KTP terdaftar per kompartemen
- Telegram Chat ID penghuni
- PIN Admin dan Master Card
- Status isi kompartemen loker

> Pengguna tetap dapat membuka loker dengan e-KTP saat koneksi internet terputus.

---

## Alur Operasional Sistem

### 1. Flowchart Alur Sistem Utama (Drop-off & Pengambilan Paket)

Diagram berikut merinci alur inisialisasi alat, proses kurir menitipkan paket (input nomor kompartemen, potret foto, kirim OTP, buka pintu), hingga proses penghuni mengambil paket (tempel e-KTP atau input OTP, buka pintu, perbarui status):

![Flowchart Alur Sistem Utama](schematics/Alur%20Sistem%20Utama.avif)

_Flowchart Alur Sistem Utama: Proses Drop-off Kurir (kiri) dan Pengambilan Penghuni (kanan)_

### 2. Flowchart Alur Sistem Mode Admin

Diagram berikut merinci akses Mode Admin melalui tombol **`A`** pada keypad untuk 6 menu konfigurasi:

![Flowchart Alur Sistem Mode Admin](schematics/Alur%20Sistem%20Mode%20Admin.avif)

_Flowchart Mode Admin: Menu Konfigurasi Lokal (1: RFID, 2: Telegram ID, 3: WiFi, 4: Reset, 5: PIN, 6: Master Card)_

---

## Skema Rangkaian & Pengkabelan

### 1. Wiring Diagram Sistem

![Wiring Diagram](schematics/Wiring_Diagram.avif)  
_Diagram Pengkabelan Komponen Loker_

### 2. Skematik Rangkaian Detail

![Skematik Rangkaian](schematics/skematik_rangkaian.avif)  
_Skematik Koneksi Pinout dan Jalur Daya_

---

## Rincian Perangkat Keras (Hardware Specs)

| Komponen                 | Spesifikasi / Model                     | Peran & Konfigurasi Pin                                               |
| ------------------------ | --------------------------------------- | --------------------------------------------------------------------- |
| **Mikrokontroler Utama** | ESP32 DevKit V1 (30 Pin)                | Pemrosesan FreeRTOS dual-core                                         |
| **Kamera Pengawas**      | ESP32-CAM + OV2640                      | Dokumentasi foto kurir via Serial UART (GPIO 1 TX, GPIO 3 RX)         |
| **Sensor Jarak Paket**   | 3x VL53L0X (Laser Time-of-Flight)       | Deteksi objek dalam kompartemen via I2C                               |
| **Sensor Status Pintu**  | 3x Magnetic Switch MC-38                | Deteksi fisik status buka/tutup pintu                                 |
| **I/O Expander**         | PCF8574 Module                          | Kontrol pin XSHUT sensor ToF (P0, P2, P4) dan baca MC-38 (P1, P3, P5) |
| **Pembaca Kartu**        | RFID RC522 (13.56 MHz)                  | Pemindaian e-KTP penghuni via bus SPI (GPIO 5, 18, 19, 23, 2)         |
| **Input Manual**         | Keypad Matrix 4x4                       | Input OTP dan navigasi Menu Admin (8 GPIO)                            |
| **Antarmuka Display**    | LCD 16x2 Karakter + I2C Backpack        | Tampilan panduan pengguna dan status sistem (SDA 21, SCL 22)          |
| **Kunci Pintu**          | 3x Solenoid Door Lock 12V               | Pengunci mekanik pintu kompartemen                                    |
| **Driver Aktuator**      | Relay Module 3-Channel 5V (Optocoupler) | Saklar kendali solenoid (GPIO 4, 16, 17)                              |
| **Indikator Audio**      | Active Buzzer 5V                        | Nada konfirmasi input                                                 |
| **Catu Daya Utama**      | Power Supply Switching 12V 5A DC        | Sumber daya solenoid dan sistem                                       |
| **Regulator Tegangan**   | Buck Converter LM2596 (Step-Down 5V)    | Penurun tegangan 12V ke 5V untuk ESP32 dan modul sensor               |
| **Material Bodi**        | Plywood Kayu 15mm                       | Bodi non-logam agar tidak mengganggu sinyal RFID                      |

---

## Arsitektur Perangkat Lunak & FreeRTOS

Firmware dikembangkan pada lingkungan **Arduino IDE (C/C++)** menggunakan kernel **FreeRTOS**:

### 1. Pembagian Task Dual-Core

- **Task Core 0 (`TaskTelegram` & `TaskNetwork`):**
  - Prioritas menengah-rendah, siklus polling pesan Telegram asinkron.
  - Transmisi HTTP multipart saat mengirim foto dari ESP32-CAM ke Bot Telegram.
  - Manajemen timer kedaluwarsa kode OTP.
- **Task Core 1 (`TaskHardware` & `TaskUI`):**
  - Prioritas tinggi real-time.
  - Polling pemindaian RFID RC522 dan pembacaan matriks keypad tanpa jeda blocking.
  - Pengukuran jarak laser VL53L0X dan pembacaan saklar pintu MC-38.
  - Kontrol relay solenoid dan pembaruan tampilan LCD 16x2.

### 2. Mode Admin Mandiri (_Local Configuration_)

- Tekan tombol **`A`** pada keypad untuk membuka menu konfigurasi.
- Akses diverifikasi menggunakan **Master Card RFID** atau **PIN Admin**.
- Menu konfigurasi langsung via LCD dan Keypad:
  1. Registrasi e-KTP penghuni baru (simpan UID ke NVS)
  2. Pembaruan Chat ID Telegram penghuni
  3. Buka paksa (_manual unlock_) dan reset status kompartemen
  4. Ganti Master PIN atau Master Card

---

## Hasil Pengujian & Performa

Ringkasan pengujian teknis:

- **Pengujian Fungsional:** 100% berhasil pada 14 skenario pengujian autentikasi (RFID, e-KTP, PIN keypad, OTP Telegram, dan Mode Admin).
- **Stabilitas Sistem:** Nol kasus _system freeze_ atau _blocking_ selama transmisi foto dan latensi jaringan.
- **Stabilitas Catu Daya:**
  - Output LM2596 (5V): Stabil pada **4.95V – 5.02V** (deviasi 0.2% – 1.0%, batas toleransi < 5%).
  - Suplai Solenoid (12V): Tegangan terukur **11.20V – 11.23V** saat beban solenoid aktif (deviasi 6.5%, dalam batas toleransi standar catu daya).
- **Integritas Sinyal I2C:** Clock I2C diatur ke **50 kHz** dengan _averaging filter_ untuk menjaga kestabilan pembacaan sensor pada kabel panjang.

---

## Struktur Repositori

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
│   ├── Foto_Alat.avif                  # Foto fisik prototipe Smart Locker 3 pintu
│   └── Antarmuka_Bot_Telegram.avif     # Tampilan interaksi notifikasi & tombol bot Telegram
├── schematics/
│   ├── Alur Sistem Utama.avif          # Flowchart alur sistem utama (drop-off & pengambilan)
│   ├── Alur Sistem Mode Admin.avif     # Flowchart alur mode admin (6 menu konfigurasi)
│   ├── Wiring_Diagram.avif             # Diagram perkabelan komponen & sensor
│   └── skematik_rangkaian.avif         # Skema jalur rangkaian elektronik
└── README.md
```

---

## Panduan Instalasi & Setup

1. **Persiapan Perangkat Lunak:**
   - Pasang [Arduino IDE](https://www.arduino.cc/en/software) (v2.x disarankan).
   - Tambahkan URL board package ESP32: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`.
2. **Instalasi Pustaka (Libraries):**
   - `UniversalTelegramBot`
   - `ArduinoJson` (v6.x)
   - `MFRC522`
   - `Adafruit_VL53L0X`
   - `Adafruit_PCF8574`
   - `LiquidCrystal_I2C`
   - `Keypad`
3. **Konfigurasi Kredensial:**
   - Buka `firmware/smart_locker_main/smart_locker_main.ino`, lalu sesuaikan WiFi SSID, kata sandi, dan Token Bot Telegram.
4. **Perkabelan & Flash Firmware:**
   - Sambungkan pin ESP32 dan ESP32-CAM sesuai tabel konfigurasi pin dan skema di direktori `schematics/`.
   - Unggah firmware ke masing-masing mikrokontroler.

---

## Tim & Hak Kekayaan Intelektual (HKI)

Sistem Smart Locker terdaftar sebagai **Hak Cipta Program Komputer** pada **Direktorat Jenderal Kekayaan Intelektual (DJKI) Kementerian Hukum dan HAM Republik Indonesia**:

- **Pencipta Utama:** Rangga Ayi Pratama
- **Dosen Pembimbing 1:** Ica Admirani, S.Kom., M.Kom.
- **Dosen Pembimbing 2:** Della Oktaviany, S.Kom., M.T.I.
- **Institusi:** Program Studi D3 Teknik Komputer, Politeknik Negeri Sriwijaya (POLSRI), Palembang (2026)
- **Dokumen Terkait:** _Manual Penggunaan dan Spesifikasi Program Komputer, DJKI Kemenkumham RI_ (tersedia di `docs/SMART LOCKER PAKET.pdf`)

> _Hak Cipta © 2026 Rangga Ayi Pratama, Ica Admirani, Della Oktaviany. Nomor Pencatatan: EC002026135705. All rights reserved._

---

## Kontak

- **Email:** pratama.ranggaayi@gmail.com
