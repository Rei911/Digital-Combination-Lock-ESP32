# 🔐 Digital Combination Lock System
**ESP32-S3 + FreeRTOS — Multicore Task, Queue, Semaphore, Mutex, Rotary Encoder, Servo Lock & OLED**



Project ini merupakan sistem pengunci digital (combination lock) berbasis ESP32-S3 yang berjalan pada dual-core FreeRTOS.
Pengguna memasukkan kombinasi menggunakan rotary encoder, sistem menampilkan status melalui OLED, dan mekanisme penguncian dilakukan oleh servo. LED dan Buzzer digunakan sebagai indikator status LOCKED / UNLOCKED / ERROR / LOCKOUT.

Sistem dilengkapi lockout mode otomatis ketika terjadi kesalahan input sebanyak 3 kali.



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



## 🔗 Arsitektur Komunikasi Antar Task

### 📨 1. Queue — komunikasi antar-task (Verify → LockControl)
Mengirim perintah:
- `CMD_UNLOCK`
- `CMD_WRONG`
- `CMD_LOCKOUT`

LockControlTask memproses seluruh perintah ini untuk:
- Membuka / menutup servo
- Mengatur LED
- Membunyikan buzzer

### 🔒 2. Mutex — proteksi variabel kode
Melindungi akses variabel:
- `entered`
- `savedCode`
- `lockState`
  
Digunakan oleh:
- TaskEncoder
- TaskOLED
- TaskVerify 

### 🚦 3. Binary Semaphore — trigger verifikasi
- TaskEncoder memberikan sinyal setiap kali 4 digit telah selesai
- TaskVerify menangkap sinyal dan memproses kode

### 🔁 4. Shared Variables
- `lockState` (LOCKED / UNLOCKED / ERROR / LOCKOUT)
- `digits[4]` (digit aktif)
- `digitIndex`
- `errorCount`
- `encoderValue`



## ⚙️ Metode yang Dipakai

| Task            | Core | Fungsi                          | Prioritas |
| --------------- | ---- | ------------------------------- | --------- |
| TaskEncoder     | 1    | Membaca rotary encoder & tombol | 3         |
| TaskOLED        | 1    | Menampilkan status ke OLED      | 2         |
| TaskVerify      | 0    | Memvalidasi kode                | 4         |
| TaskLockControl | 0    | Servo + LED + buzzer            | 5         |
| TaskButton1     | 1    | Reset input manual              | 2         |

- Queue untuk komunikasi event verifikasi
- Semaphore untuk trigger verifikasi
- Mutex untuk proteksi variabel kode
- Servo PWM 50Hz
- Buzzer tone manual (tanpa timer konflik dengan PWM) 



## 🧩 Input dan Output Sistem

### Input
- Rotary Encoder CW/CCW → memilih digit 0..9
- Encoder SW → konfirmasi digit / lanjut ke digit berikutnya
- Reset Button → reset kombinasi & status

### Output
- Servo :
  - 0° → Locked
  - 90° → Unlocked
- LED:
  - Merah → Locked
  - Hijau → Unlocked
  - Biru → Error / Lockout
- Buzzer:
  - 1 beep → unlock
  - 3 beep → wrong code
  - Alarm 10 detik → lockout
- OLED:
  - Menampilkan digit aktif
  - Status sistem secara real-time



## 🚀 Cara Kerja Sistem

1. Putar rotary encoder → memilih angka (digit 0–3).
2. Tekan encoder → berpindah ke digit berikutnya.
3. Setelah 4 digit selesai → TaskVerify aktif.
4. Jika kode benar:
- Servo membuka
- LED hijau ON
- Buzzer beep
- 8 detik kemudian servo mengunci kembali
5. Jika kode salah:
- Error + buzzer 3x
- LED biru sebentar
- digit reset
6. Jika salah 3 kali:
- Masuk LOCKOUT 10 detik
- LED biru nyala
- Buzzer alarm
- Sistem kembali LOCKED



**Wiring simulasi di wokwi**
<img width="741" height="639" alt="image" src="https://github.com/user-attachments/assets/f1cd5d82-b4d3-4e9f-b74e-804f3bc0d16c" />


**Video simulasi**


https://github.com/user-attachments/assets/45314a1e-c9ea-45d7-b2e6-00caf5a18394

