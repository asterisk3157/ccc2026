// =====================================================================
// CCC2026 電子工作班 - バーチャル輪投げ データ収集ファーム
// =====================================================================
// 対象:
//   Arduino Pro Micro 5V/16MHz + MPU6050
//
// 配線:
//   MPU6050 SDA -> D2, SCL -> D3, VCC -> VCC, GND -> GND
//   投げ開始ボタン -> D5 (INPUT_PULLUP)
//   振動モーター -> D6 (任意)
//   赤LED -> D7 (任意)
//   青LED -> D9 (任意)
//
// 1ボタン動作:
//   1. D5を押す     -> 測定開始、前回状態をリセット
//   2. 押したまま引く -> 最初のBASELINE_MSでジャイロ平均を基準にする
//   3. 押したまま振る -> ジャイロ3軸を送信
//   4. D5を離す     -> 測定終了
//
// 出力:
//   115200bps / 改行区切りCSV風テキスト
//   READY,mode=one_button_gyro,rate=<rate_hz>,baseline_ms=<baseline_ms>
//   BEGIN,<trial>,<rate_hz>,<baseline_ms>
//   S,<trial>,<seq>,<t_ms>,<phase>,<gx>,<gy>,<gz>
//   BASE,<trial>,<samples>,<gx0>,<gy0>,<gz0>
//   END,<trial>,<samples>,<duration_ms>,<reason>
// =====================================================================

#include <Wire.h>
#include <MPU6050_tockn.h>

MPU6050 mpu6050(Wire);

const uint8_t PIN_TRIGGER = 5;
const uint8_t PIN_MOTOR = 6;
const uint8_t PIN_LED_RED = 7;
const uint8_t PIN_LED_BLUE = 9;

const uint16_t SAMPLE_RATE_HZ = 100;
const uint16_t BASELINE_MS = 500;
const uint16_t MAX_CAPTURE_MS = 2600;
const uint32_t SAMPLE_INTERVAL_US = 1000000UL / SAMPLE_RATE_HZ;

enum CaptureState {
  STATE_IDLE,
  STATE_CAPTURING
};

CaptureState captureState = STATE_IDLE;

uint16_t trialId = 0;
uint16_t sampleSeq = 0;
uint32_t captureStartMs = 0;
uint32_t lastSampleUs = 0;
bool lastTriggerDown = false;
bool baselineSent = false;

float sumGx = 0.0f;
float sumGy = 0.0f;
float sumGz = 0.0f;
uint16_t baselineSamples = 0;

void setup() {
  pinMode(PIN_TRIGGER, INPUT_PULLUP);
  pinMode(PIN_MOTOR, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);

  digitalWrite(PIN_MOTOR, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_BLUE, LOW);

  Serial.begin(115200);
  while (!Serial) {
    ; // Pro Micro のUSBシリアル待ち。単独運用時はここを外してよい。
  }

  Wire.begin();
  mpu6050.begin();
  mpu6050.calcGyroOffsets(false);

  Serial.print("READY,mode=one_button_gyro,rate=");
  Serial.print(SAMPLE_RATE_HZ);
  Serial.print(",baseline_ms=");
  Serial.println(BASELINE_MS);
}

void loop() {
  mpu6050.update();

  const bool triggerDown = digitalRead(PIN_TRIGGER) == LOW;

  if (captureState == STATE_IDLE) {
    if (triggerDown && !lastTriggerDown) {
      beginCapture();
    }
  } else {
    captureLoop(triggerDown);
  }

  lastTriggerDown = triggerDown;
}

void beginCapture() {
  captureState = STATE_CAPTURING;
  trialId++;
  sampleSeq = 0;
  captureStartMs = millis();
  lastSampleUs = micros();
  baselineSent = false;

  sumGx = 0.0f;
  sumGy = 0.0f;
  sumGz = 0.0f;
  baselineSamples = 0;

  digitalWrite(PIN_LED_BLUE, HIGH);
  digitalWrite(PIN_LED_RED, LOW);

  Serial.print("BEGIN,");
  Serial.print(trialId);
  Serial.print(",");
  Serial.print(SAMPLE_RATE_HZ);
  Serial.print(",");
  Serial.println(BASELINE_MS);
}

void captureLoop(bool triggerDown) {
  const uint32_t elapsedMs = millis() - captureStartMs;

  while ((uint32_t)(micros() - lastSampleUs) >= SAMPLE_INTERVAL_US) {
    lastSampleUs += SAMPLE_INTERVAL_US;
    writeSample();
  }

  if (!triggerDown) {
    endCapture("release");
    return;
  }

  if (elapsedMs >= MAX_CAPTURE_MS) {
    endCapture("timeout");
  }
}

void writeSample() {
  const uint32_t elapsedMs = millis() - captureStartMs;
  const bool isBaseline = elapsedMs <= BASELINE_MS;

  const float gx = mpu6050.getGyroX();
  const float gy = mpu6050.getGyroY();
  const float gz = mpu6050.getGyroZ();

  if (isBaseline) {
    sumGx += gx;
    sumGy += gy;
    sumGz += gz;
    baselineSamples++;
  } else if (!baselineSent) {
    sendBaseline();
  }

  Serial.print("S,");
  Serial.print(trialId);
  Serial.print(",");
  Serial.print(sampleSeq++);
  Serial.print(",");
  Serial.print(elapsedMs);
  Serial.print(",");
  Serial.print(isBaseline ? "baseline" : "throw");
  Serial.print(",");
  printFloat(gx);
  Serial.print(",");
  printFloat(gy);
  Serial.print(",");
  printFloat(gz);
  Serial.println();
}

void sendBaseline() {
  baselineSent = true;

  const float n = baselineSamples > 0 ? (float)baselineSamples : 1.0f;

  Serial.print("BASE,");
  Serial.print(trialId);
  Serial.print(",");
  Serial.print(baselineSamples);
  Serial.print(",");
  printFloat(sumGx / n);
  Serial.print(",");
  printFloat(sumGy / n);
  Serial.print(",");
  printFloat(sumGz / n);
  Serial.println();
}

void endCapture(const char *reason) {
  if (!baselineSent) {
    sendBaseline();
  }

  const uint32_t durationMs = millis() - captureStartMs;

  Serial.print("END,");
  Serial.print(trialId);
  Serial.print(",");
  Serial.print(sampleSeq);
  Serial.print(",");
  Serial.print(durationMs);
  Serial.print(",");
  Serial.println(reason);

  digitalWrite(PIN_LED_BLUE, LOW);
  digitalWrite(PIN_LED_RED, HIGH);
  delay(45);
  digitalWrite(PIN_LED_RED, LOW);

  captureState = STATE_IDLE;
}

void printFloat(float value) {
  Serial.print(value, 4);
}
