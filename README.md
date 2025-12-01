# 🔐 Digital Combination Lock System
**ESP32-S3 + FreeRTOS — Multicore Task, Queue, Semaphore, Mutex, Rotary Encoder, Servo Lock & OLED**

.............................................................................................................

Project ini merupakan sistem pengunci digital (combination lock) berbasis ESP32-S3 yang berjalan pada dual-core FreeRTOS.
Pengguna memasukkan kombinasi menggunakan rotary encoder, sistem menampilkan status melalui OLED, dan mekanisme penguncian dilakukan oleh servo. LED dan Buzzer digunakan sebagai indikator status LOCKED / UNLOCKED / ERROR / LOCKOUT.

Sistem dilengkapi lockout mode otomatis ketika terjadi kesalahan input sebanyak 3 kali.

.............................................................................................................

## 📦 Komponen yang Digunakan

| Komponen                     | Fungsi                                      |
| ---------------------------- | ------------------------------------------- |
| ESP32-S3 Dev Board           | Proses utama, menjalankan multitasking RTOS |
| Rotary Encoder + Push Button | Input kombinasi 4 digit                     |
| SSD1306 OLED                 | Tampilan kode dan status                    |
| Servo Motor                  | Mengunci / membuka mekanisme lock           |
| LED (Merah, Hijau, Biru)     | Indikator sistem                            |
| Buzzer                       | Notifikasi dan alarm                        |
| Push Button Reset            | Mereset input kombinasi                     |
| Catu daya 5V                 | Power servo dan ESP32                       |


**GPIO Mapping**

| Fungsi       | GPIO |
| ------------ | ---- |
| LED Locked   | 2    |
| LED Unlocked | 15   |
| LED Error    | 16   |
| Reset Button | 13   |
| Buzzer       | 14   |
| Encoder CLK  | 18   |
| Encoder DT   | 19   |
| Encoder SW   | 21   |
| Servo PWM    | 10   |
| OLED SDA     | 8    |
| OLED SCL     | 9    |


.............................................................................................................

## 🛠 Fitur Utama

- Memasukkan kode melalui rotary encoder
- Tampilan OLED real-time: digit aktif & status sistem
- Verifikasi otomatis ketika 4 digit selesai
- Servo lock mechanism: LOCK ↔ UNLOCK otomatis
- 3 Level LED Indicator: locked / unlocked / error
- Lockout Mode 10 detik setelah salah 3 kali
- Multicore Processing:
  - Core 1 → Encoder + OLED + Input
  - Core 0 → Verification + Servo Control
- Sinkronisasi thread menggunakan Queue, Semaphore, dan Mutex
- Debug lengkap via Serial Monitor

.............................................................................................................

## 🔗 Arsitektur Komunikasi Antar Task

### 📨 1. Queue — komunikasi OPEN/CLOSE ke ServoTask
- ISR mengirim perintah `CMD_OPEN` / `CMD_CLOSE` ke **gateQueue**  
- Servo task menunggu dan memproses perintah ini  
- Queue dibersihkan saat emergency aktif

### 🔒 2. Mutex — proteksi eksklusif servo
- Servo tidak boleh dikendalikan dua task sekaligus  
- ServoTask mengambil mutex sebelum menggerakkan servo  
- EmergencyMonitorTask bisa menghentikan servo dengan mutex  

### 🚨 3. Binary Semaphore — Emergency ON/OFF
- ISR tombol emergency memanggil `xSemaphoreGiveFromISR()`  
- EmergencyMonitorTask menangkap sinyal tersebut  
- Toggle otomatis ON/OFF

### 🔁 4. Shared Variable
- `emergency_activated` → status emergency  
- `gate_is_open` → status servo  
- `blocked_count` → jumlah perintah diblokir  

.............................................................................................................

## ⚙️ Metode yang Dipakai

| Task                 | Core | Fungsi                        | Prioritas |
|----------------------|------|-------------------------------|-----------|
| Servo Task           | 0    | Menangani OPEN/CLOSE          | 3         |
| Buzzer Task          | 0    | Mode alarm                     | 2         |
| Emergency Monitor    | 1    | Mengawasi tombol emergency     | 4         |
| LED Task             | 1    | Indikator status               | 1         |

- ISR Button untuk input cepat  
- PWM Servo (50Hz) & Buzzer (dynamik freq)  
- Emergency blocking → queue dibersihkan, servo berhenti, LED & buzzer warning mode  

.............................................................................................................

## 🧩 Input dan Output Sistem

### Input
- BTN_OPEN → kirim CMD_OPEN  
- BTN_CLOSE → kirim CMD_CLOSE  
- BTN_EMERGENCY → toggle emergency mode  

### Output
- Servo Motor → 0° (close) / 90° (open), pergerakan cepat bertahap  
- Buzzer → Normal = silent, Emergency = beep cepat  
- LED → Hijau = open, Merah = closed, Emergency = kedip cepat  
- Serial Monitor → debug semua aktivitas  

.............................................................................................................

## 🚀 Cara Kerja Sistem

1. Tekan **OPEN** → ISR → Queue → Servo buka  
2. Tekan **CLOSE** → ISR → Queue → Servo tutup  
3. Tekan **EMERGENCY** → Servo berhenti, queue dikosongkan, LED & buzzer mode darurat  
4. Tekan **EMERGENCY lagi** → Sistem kembali normal  

.............................................................................................................


**Wiring simulasi di wokwi**
<img width="741" height="639" alt="image" src="https://github.com/user-attachments/assets/f1cd5d82-b4d3-4e9f-b74e-804f3bc0d16c" />


**Video simulasi**


https://github.com/user-attachments/assets/45314a1e-c9ea-45d7-b2e6-00caf5a18394

