#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= PIN DEFINISI =================
#define LED1 2    // Locked
#define LED2 15   // Unlocked
#define LED3 16   // Error

#define BUTTON1 13 // Reset input (optional)

#define BUZZER_PIN 14

#define ENC_CLK 18
#define ENC_DT  19
#define ENC_SW  21

#define SERVO_PIN 10
Servo myServo;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==== Global Resource (Queue, Semaphore, Mutex) ====
QueueHandle_t qEncoder;
QueueHandle_t qCommand;
SemaphoreHandle_t semVerify;
SemaphoreHandle_t mutexCode;

// ==== Code Storage ====
String savedCode = "1234";
String entered = "";
int currentDigit = 0;

enum LockState_t { STATE_LOCKED, STATE_UNLOCKED, STATE_ERROR, STATE_LOCKOUT };
volatile LockState_t lockState = STATE_LOCKED;

volatile int encoderValue = 0;
int lastClk = 0;


int errorCount = 0;
const int MAX_ERRORS = 3;
const int CODE_LEN = 4;
const int UNLOCK_TIMEOUT_MS = 8000; // 8 detik

// Servo angles
const int SERVO_LOCK_ANGLE = 0;
const int SERVO_UNLOCK_ANGLE = 90;

int digitIndex = 0;       // digit aktif (0..3)
int digits[4] = {0,0,0,0}; // masing-masing digit 0–9

// Encoder event codes
//  1 = CW (increase digit)
// -1 = CCW (decrease digit)
//  2 = SW pressed (confirm/save digit)
typedef int16_t encoder_event_t;

// Forward declarations
void TaskEncoder(void *parameter);
void TaskOLED(void *parameter);
void TaskVerify(void *parameter);
void TaskButton1(void *parameter);
void TaskLockControl(void *parameter);
void setupTasks();

enum LockCommand { CMD_UNLOCK, CMD_WRONG, CMD_LOCKOUT };





// ---------------- Utility helpers ----------------
void buzzerTone(int freq, int duration_ms) {
    int period_us = 1000000 / freq;  // periode gelombang
    int half = period_us / 2;

    unsigned long start = millis();
    while (millis() - start < duration_ms) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(half);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(half);
    }
}

void buzzerBeep(int duration_ms = 150) {
    buzzerTone(1500, duration_ms);
}

void buzzerBeepTimes(int times, int duration_ms = 150, int gap_ms = 100) {
    for (int i = 0; i < times; i++) {
        buzzerTone(1500, duration_ms);
        vTaskDelay(gap_ms / portTICK_PERIOD_MS);
    }
}


void resetPassword() {
    // Reset digit input
    for (int i = 0; i < 4; i++) digits[i] = 0;
    digitIndex = 0;

    // Reset error counter
    errorCount = 0;

    // Reset entered code
    if (xSemaphoreTake(mutexCode, portMAX_DELAY)) {
        entered = "";
        xSemaphoreGive(mutexCode);
    }

    // Kembalikan state ke LOCKED
    lockState = STATE_LOCKED;

    Serial.println("🔄 resetPassword(): Input dan error direset.");
}



// =====================================================
// =============== TASK DEFINISI ========================
// =====================================================
// --- TaskButton1: reset input when BUTTON1 pressed ---
void TaskButton1(void *pvParameters) {
    pinMode(BUTTON1, INPUT_PULLUP);

    for (;;) {
        if (digitalRead(BUTTON1) == LOW) {
            // Tombol ditekan (aktif LOW)
            resetPassword();
            Serial.println("Password reset oleh BUTTON1");

            // Biar tidak double-trigger
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


// --- TaskOLED: tampilkan entered code + status ---
void TaskOLED(void *parameter) {
  Serial.printf("✅ OLED Task Running on Core %d\n", xPortGetCoreID());
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("❌ OLED gagal diinisialisasi");
    vTaskDelete(NULL);
  }

  for (;;) {
    // copy protected data
    String enteredCopy;
    LockState_t stateCopy;
    if(xSemaphoreTake(mutexCode, (TickType_t)10) == pdTRUE) {
      enteredCopy = entered;
      stateCopy = lockState;
      xSemaphoreGive(mutexCode);
    } else {
      enteredCopy = entered;
      stateCopy = lockState;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Digital Combination Lock");
    display.println("----------------------");
    display.print("Enter Code: ");
    for (int i=0; i<4; i++) {
      if (i == digitIndex) display.print("[");
      else display.print(" ");
    
      display.print(digits[i]);
    
      if (i == digitIndex) display.print("]");
      else display.print(" ");
    
      display.print(" ");
    }

    display.println();
    display.println();
    display.print("Status: ");
    switch(stateCopy){
      case STATE_LOCKED: display.println("LOCKED"); break;
      case STATE_UNLOCKED: display.println("UNLOCKED"); break;
      case STATE_ERROR: display.println("ERROR"); break;
      case STATE_LOCKOUT: display.println("LOCKOUT"); break;
    }
    display.display();
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// --- TaskEncoder: baca rotary encoder, kirim event ke queue ---
void TaskEncoder(void *parameter) {
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT, INPUT);
  pinMode(ENC_SW, INPUT_PULLUP);

  int lastCLK = digitalRead(ENC_CLK);

  for(;;) {
    int currentCLK = digitalRead(ENC_CLK);

    // ROTATION
    if (lastCLK == LOW && currentCLK == HIGH) {
      if (digitalRead(ENC_DT) == HIGH)
        digits[digitIndex] = (digits[digitIndex] + 1) % 10;   // CW
      else
        digits[digitIndex] = (digits[digitIndex] + 9) % 10;   // CCW

      Serial.printf("Digit[%d] = %d\n", digitIndex, digits[digitIndex]);
    }
    lastCLK = currentCLK;

    // BUTTON PRESS → lock digit
    if (digitalRead(ENC_SW) == LOW) {
      Serial.println("SW pressed → next digit");

      digitIndex++;

      if (digitIndex >= 4) {
        digitIndex = 0;

        // Convert digits to string
        String code = "";
        for (int i=0; i<4; i++) code += String(digits[i]);

        Serial.println("CODE ENTERED = " + code);

        // Copy into global “entered”
        if (xSemaphoreTake(mutexCode, portMAX_DELAY)) {
          entered = code;
          xSemaphoreGive(mutexCode);
        }

        // Trigger verification
        xSemaphoreGive(semVerify);
      }

      vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
}



// --- TaskVerify: menunggu semVerify, verifikasi kode, kontrol servo/buzzer/led ---
void TaskVerify(void *p){
  for(;;){
    // tunggu encoder menekan tombol 4x (kode lengkap)
    if(xSemaphoreTake(semVerify, portMAX_DELAY)){

      String codeCopy;
      String savedCopy;

      // copy protected
      if(xSemaphoreTake(mutexCode, portMAX_DELAY)){
        codeCopy = entered;
        savedCopy = savedCode;
        entered = "";       // reset buffer
        xSemaphoreGive(mutexCode);
      }

      Serial.println("🔍 VERIFY CODE = " + codeCopy);

      // --------------------
      // 1. Cek jika benar
      // --------------------
      if(codeCopy == savedCopy){
        errorCount = 0;
        LockCommand cmd = CMD_UNLOCK;
        xQueueSend(qCommand, &cmd, 0);
        continue;
        
      }

      // --------------------
      // 2. Salah → tambahkan error
      // --------------------
      errorCount++;
      Serial.println("❌ WRONG CODE!");

      if(errorCount >= MAX_ERRORS){
        LockCommand cmd = CMD_LOCKOUT;
        xQueueSend(qCommand, &cmd, 0);
      } 
      else {
        LockCommand cmd = CMD_WRONG;
        xQueueSend(qCommand, &cmd, 0);
      }
    }
  }
}



void TaskLockControl(void *parameter){
  LockCommand cmd;
  Serial.println("🔔 TaskLockControl started - waiting for commands...");

  for(;;){
    if(xQueueReceive(qCommand, &cmd, portMAX_DELAY)){
      Serial.printf("🎯 LockControl received command: %d\n", cmd);
      Serial.println("📍 Current lockState: " + String(lockState));

      if(cmd == CMD_UNLOCK){
        Serial.println("✅ CODE BENAR - MEMBUKA KUNCI");
        
        // Update state
        lockState = STATE_UNLOCKED;
        Serial.println("🔄 State changed to UNLOCKED");
        
        // LED: matikan LED1, nyalakan LED2
        digitalWrite(LED1, LOW);
        digitalWrite(LED2, HIGH);
        Serial.println("💡 LED: RED OFF, GREEN ON");
        
        // Bunyi konfirmasi
        Serial.println("🔊 Playing beep...");
        buzzerBeep(300);
        Serial.println("🔊 Beep completed");
        noTone(BUZZER_PIN);   // hentikan tone agar timer servo tidak ketimpa
        vTaskDelay(20);
        // Gerakkan servo ke UNLOCK - dengan debugging ekstra
        Serial.printf("🔄 Moving servo to UNLOCK position: %d degrees\n", SERVO_UNLOCK_ANGLE);
        
        // Test servo dengan cara berbeda
        Serial.println("🔧 Checking servo attachment...");
        if(myServo.attached()) {
          Serial.println("✅ Servo is attached");
        } else {
          Serial.println("❌ Servo is NOT attached - reattaching...");
          myServo.attach(SERVO_PIN, 500, 2400);
        }
        
        // Gerakkan servo secara bertahap untuk testing
        Serial.println("🎬 Starting servo movement...");
        myServo.write(SERVO_UNLOCK_ANGLE);
        Serial.println("📝 Servo write command sent");
        
        // Beri waktu untuk servo bergerak
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        Serial.println("🔓 SERVO SHOULD BE UNLOCKED NOW - Checking movement");
        
        // Tunggu 8 detik dalam state UNLOCKED
        Serial.println("⏳ Waiting 8 seconds in UNLOCKED state...");
        for(int i = 8; i > 0; i--) {
          Serial.println("⏰ " + String(i) + " seconds remaining...");
          vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        
        // Otomatis kunci kembali
        Serial.printf("🔄 Moving servo to LOCK position: %d degrees\n", SERVO_LOCK_ANGLE);
        myServo.write(SERVO_LOCK_ANGLE);
        Serial.println("🔒 SERVO LOCKED");
        
        // Kembali ke state LOCKED
        digitalWrite(LED2, LOW);
        digitalWrite(LED1, HIGH);
        lockState = STATE_LOCKED;
        Serial.println("💡 LED: GREEN OFF, RED ON - Back to LOCKED state");
        Serial.println("🔄 State changed to LOCKED");
      }

      else if(cmd == CMD_WRONG){
        Serial.println("❌ CODE SALAH - Processing wrong code");
        lockState = STATE_ERROR;
        
        digitalWrite(LED3, HIGH);
        Serial.println("💡 LED: BLUE ON (Error)");
        buzzerBeepTimes(3);
        Serial.println("🔊 Buzzer beeped 3 times");
        digitalWrite(LED3, LOW);
        Serial.println("💡 LED: BLUE OFF");

        // Reset input digit
        for (int i = 0; i < 4; i++) digits[i] = 0;
        digitIndex = 0;
              
        // Reset entered string
        if (xSemaphoreTake(mutexCode, portMAX_DELAY)) {
            entered = "";
            xSemaphoreGive(mutexCode);
        }

        
        lockState = STATE_LOCKED;
      }

      else if(cmd == CMD_LOCKOUT){
        Serial.println("🚨 LOCKOUT 10 DETIK!");
        lockState = STATE_LOCKOUT;

        digitalWrite(LED3, HIGH);
        Serial.println("💡 LED: BLUE ON (Lockout)");

        // Alarm 10 detik
        Serial.println("🚨 BUZZER ALARM for 10 seconds");
        for(int i = 0; i < 100; i++) {
          buzzerTone(900, 50);
          vTaskDelay(50 / portTICK_PERIOD_MS);
        }

        digitalWrite(LED3, LOW);
        Serial.println("💡 LED: BLUE OFF");

        // reset error & input
        errorCount = 0;
        entered = "";

        lockState = STATE_LOCKED;
        digitalWrite(LED1, HIGH);
        myServo.write(SERVO_LOCK_ANGLE);
        
        Serial.println("🔓 LOCKOUT SELESAI - System reset");
      }
    }
  }
}




// =====================================================
// =================== SETUP ============================
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n===== Combination Lock - INIT =====");

  // -------------------------------
  // 1. Setup pin awal
  // -------------------------------
  pinMode(LED1, OUTPUT); 
  pinMode(LED2, OUTPUT); 
  pinMode(LED3, OUTPUT); 
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED1, HIGH);   // LOCKED (sesuai flowchart)
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

  // -------------------------------
  // 2. Servo inisialisasi (LOCKED)
  // -------------------------------
  Serial.println("🔧 Initializing servo...");
  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(SERVO_LOCK_ANGLE);
  Serial.println("✅ Servo initialized to LOCK position.");

  // -------------------------------
  // 3. Encoder siap
  // -------------------------------
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT, INPUT);
  pinMode(ENC_SW, INPUT_PULLUP);

  // -------------------------------
  // 4. OLED inisialisasi
  // -------------------------------
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("❌ OLED Gagal");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("Digital Combination Lock");
    display.println("----------------");
    display.println("Status: LOCKED");
    display.println("Enter Code...");
    display.display();
  }

  // -------------------------------
  // 5. Inisialisasi kode & status
  // -------------------------------
  entered = "";              
  digitIndex = 0;
  lockState = STATE_LOCKED;  // tampilan OLED juga baca ini

  // -------------------------------
  // 6. Queue & Semaphore 
  // -------------------------------
  qEncoder = xQueueCreate(10, sizeof(encoder_event_t));
  semVerify = xSemaphoreCreateBinary();
  mutexCode = xSemaphoreCreateMutex();
  qCommand = xQueueCreate(5, sizeof(LockCommand));

  // -------------------------------
  // 7. Create Tasks
  // -------------------------------
  xTaskCreatePinnedToCore(TaskEncoder, "Encoder", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(TaskOLED, "OLED", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskLockControl, "LockCtrl", 4096, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(TaskVerify, "Verify", 4096, NULL, 4, NULL, 0);

  // Optional:
  xTaskCreatePinnedToCore(TaskButton1, "Btn1", 2048, NULL, 2, NULL, 1);
  
}


// ================= LOOP =================
void loop() {
  // small idle so loop doesn't hog CPU
  vTaskDelay(10 / portTICK_PERIOD_MS);
}
