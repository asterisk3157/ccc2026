// =====================================================================
// CCC2026 電子工作班 - バーチャル輪投げ 推論ファーム
// =====================================================================
// 対象:
//   Arduino Pro Micro 5V/16MHz + MPU6050
//
// 使い方:
//   1. tools/ringtoss_train.py で生成した ringtoss_model_coefficients.h を
//      このスケッチフォルダの同名ファイルへ上書きする
//   2. Pro Micro に書き込む
//   3. D5ボタンを押したまま手前に引き、前に振り、振り終わったら離す
//
// 出力:
//   RING,<vx>,<vy>,<vz>,<strength>,<class_id>,<samples>
// =====================================================================

#include <Wire.h>
#include <MPU6050_tockn.h>
#include "ringtoss_model_coefficients.h"

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

struct CaptureStats {
  float baseGx;
  float baseGy;
  float baseGz;
  float sumBaseGx;
  float sumBaseGy;
  float sumBaseGz;
  uint16_t baselineCount;
  bool baselineReady;

  uint16_t throwCount;
  uint32_t throwStartMs;
  uint32_t lastThrowMs;
  uint32_t prevThrowMs;

  float peakGyro;
  float peakGx;
  float peakGy;
  float peakGz;
  uint32_t peakMs;

  float sumGx;
  float sumGy;
  float sumGz;
  float intGx;
  float intGy;
  float intGz;
  float maxGx;
  float maxGy;
  float maxGz;
  float minGx;
  float minGy;
  float minGz;
};

CaptureState captureState = STATE_IDLE;
CaptureStats stats;

uint32_t captureStartMs = 0;
uint32_t lastSampleUs = 0;
bool lastTriggerDown = false;

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
    ;
  }

  Wire.begin();
  mpu6050.begin();
  mpu6050.calcGyroOffsets(false);

  Serial.print("READY,mode=one_button_gyro,model_ready=");
  Serial.println(RINGTOSS_MODEL_READY);
  if (!RINGTOSS_MODEL_READY) {
    Serial.println("WARN,placeholder_model");
  }
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
  resetStats();
  captureStartMs = millis();
  lastSampleUs = micros();
  captureState = STATE_CAPTURING;

  digitalWrite(PIN_LED_BLUE, HIGH);
  digitalWrite(PIN_LED_RED, LOW);
  Serial.println("BEGIN");
}

void captureLoop(bool triggerDown) {
  const uint32_t elapsedMs = millis() - captureStartMs;

  while ((uint32_t)(micros() - lastSampleUs) >= SAMPLE_INTERVAL_US) {
    lastSampleUs += SAMPLE_INTERVAL_US;
    updateStats();
  }

  if (!triggerDown) {
    finishPrediction("release");
    return;
  }

  if (elapsedMs >= MAX_CAPTURE_MS) {
    finishPrediction("timeout");
  }
}

void resetStats() {
  stats.baseGx = 0.0f;
  stats.baseGy = 0.0f;
  stats.baseGz = 0.0f;
  stats.sumBaseGx = 0.0f;
  stats.sumBaseGy = 0.0f;
  stats.sumBaseGz = 0.0f;
  stats.baselineCount = 0;
  stats.baselineReady = false;

  stats.throwCount = 0;
  stats.throwStartMs = 0;
  stats.lastThrowMs = 0;
  stats.prevThrowMs = 0;

  stats.peakGyro = 0.0f;
  stats.peakGx = 0.0f;
  stats.peakGy = 0.0f;
  stats.peakGz = 0.0f;
  stats.peakMs = 0;

  stats.sumGx = 0.0f;
  stats.sumGy = 0.0f;
  stats.sumGz = 0.0f;
  stats.intGx = 0.0f;
  stats.intGy = 0.0f;
  stats.intGz = 0.0f;
  stats.maxGx = -100000.0f;
  stats.maxGy = -100000.0f;
  stats.maxGz = -100000.0f;
  stats.minGx = 100000.0f;
  stats.minGy = 100000.0f;
  stats.minGz = 100000.0f;
}

void updateStats() {
  const uint32_t elapsedMs = millis() - captureStartMs;
  const float rawGx = mpu6050.getGyroX();
  const float rawGy = mpu6050.getGyroY();
  const float rawGz = mpu6050.getGyroZ();

  if (elapsedMs <= BASELINE_MS) {
    stats.sumBaseGx += rawGx;
    stats.sumBaseGy += rawGy;
    stats.sumBaseGz += rawGz;
    stats.baselineCount++;
    return;
  }

  if (!stats.baselineReady) {
    finalizeBaseline();
  }

  const float gx = rawGx - stats.baseGx;
  const float gy = rawGy - stats.baseGy;
  const float gz = rawGz - stats.baseGz;
  const float gyroMag = sqrt(gx * gx + gy * gy + gz * gz);

  float dt = 0.0f;
  if (stats.throwCount == 0) {
    stats.throwStartMs = elapsedMs;
  } else {
    dt = (elapsedMs - stats.prevThrowMs) / 1000.0f;
  }
  stats.prevThrowMs = elapsedMs;
  stats.lastThrowMs = elapsedMs;
  stats.throwCount++;

  if (gyroMag > stats.peakGyro) {
    stats.peakGyro = gyroMag;
    stats.peakGx = gx;
    stats.peakGy = gy;
    stats.peakGz = gz;
    stats.peakMs = elapsedMs;
  }

  stats.sumGx += gx;
  stats.sumGy += gy;
  stats.sumGz += gz;
  stats.intGx += gx * dt;
  stats.intGy += gy * dt;
  stats.intGz += gz * dt;
  stats.maxGx = max(stats.maxGx, gx);
  stats.maxGy = max(stats.maxGy, gy);
  stats.maxGz = max(stats.maxGz, gz);
  stats.minGx = min(stats.minGx, gx);
  stats.minGy = min(stats.minGy, gy);
  stats.minGz = min(stats.minGz, gz);
}

void finalizeBaseline() {
  const float n = stats.baselineCount > 0 ? (float)stats.baselineCount : 1.0f;
  stats.baseGx = stats.sumBaseGx / n;
  stats.baseGy = stats.sumBaseGy / n;
  stats.baseGz = stats.sumBaseGz / n;
  stats.baselineReady = true;
}

void finishPrediction(const char *reason) {
  if (!stats.baselineReady) {
    finalizeBaseline();
  }

  if (stats.throwCount == 0) {
    Serial.print("ERR,no_throw_samples,");
    Serial.println(reason);
    stopCaptureIndicators();
    captureState = STATE_IDLE;
    return;
  }

  float features[RINGTOSS_FEATURE_COUNT];
  buildFeatureVector(features);

  float vx = 0.0f;
  float vy = 0.0f;
  float vz = 0.0f;
  float strength = 0.0f;
  uint8_t classId = 4;

#if RINGTOSS_MODEL_READY
  classId = predictClass(features);
  predictVector(features, &vx, &vy, &vz, &strength);
#else
  predictFallback(features, &vx, &vy, &vz, &strength, &classId);
#endif

  Serial.print("RING,");
  printFloat(vx);
  Serial.print(",");
  printFloat(vy);
  Serial.print(",");
  printFloat(vz);
  Serial.print(",");
  printFloat(strength);
  Serial.print(",");
  Serial.print(classId);
  Serial.print(",");
  Serial.println(stats.throwCount);

  stopCaptureIndicators();
  captureState = STATE_IDLE;
}

void stopCaptureIndicators() {
  digitalWrite(PIN_LED_BLUE, LOW);
  digitalWrite(PIN_LED_RED, HIGH);
  delay(45);
  digitalWrite(PIN_LED_RED, LOW);
}

void buildFeatureVector(float *features) {
  const float durationS = max((stats.lastThrowMs - stats.throwStartMs) / 1000.0f, 0.01f);
  const float n = stats.throwCount > 0 ? (float)stats.throwCount : 1.0f;
  const float releaseTS = max((stats.peakMs - stats.throwStartMs) / 1000.0f, 0.0f);
  const float releaseRatio = clampFloat(releaseTS / durationS, 0.0f, 1.0f);

  features[0] = durationS;
  features[1] = n / 100.0f;
  features[2] = releaseTS;
  features[3] = releaseRatio;
  features[4] = stats.peakGyro;
  features[5] = stats.peakGx;
  features[6] = stats.peakGy;
  features[7] = stats.peakGz;
  features[8] = stats.sumGx / n;
  features[9] = stats.sumGy / n;
  features[10] = stats.sumGz / n;
  features[11] = stats.intGx;
  features[12] = stats.intGy;
  features[13] = stats.intGz;
  features[14] = stats.maxGx;
  features[15] = stats.maxGy;
  features[16] = stats.maxGz;
  features[17] = stats.minGx;
  features[18] = stats.minGy;
  features[19] = stats.minGz;
}

uint8_t predictClass(const float *features) {
  float bestScore = -1000000.0f;
  uint8_t bestClass = 4;

  for (uint8_t cls = 0; cls < RINGTOSS_CLASS_COUNT; cls++) {
    float score = RINGTOSS_CLASS_INTERCEPT[cls];
    for (uint8_t i = 0; i < RINGTOSS_FEATURE_COUNT; i++) {
      const float normalized = (features[i] - RINGTOSS_FEATURE_MEAN[i]) / RINGTOSS_FEATURE_SCALE[i];
      score += RINGTOSS_CLASS_COEF[cls][i] * normalized;
    }
    if (score > bestScore) {
      bestScore = score;
      bestClass = cls;
    }
  }

  return bestClass;
}

void predictVector(const float *features, float *vx, float *vy, float *vz, float *strength) {
  float outputs[RINGTOSS_VECTOR_OUTPUT_COUNT];
  for (uint8_t out = 0; out < RINGTOSS_VECTOR_OUTPUT_COUNT; out++) {
    float value = RINGTOSS_VECTOR_INTERCEPT[out];
    for (uint8_t i = 0; i < RINGTOSS_FEATURE_COUNT; i++) {
      const float normalized = (features[i] - RINGTOSS_FEATURE_MEAN[i]) / RINGTOSS_FEATURE_SCALE[i];
      value += RINGTOSS_VECTOR_COEF[out][i] * normalized;
    }
    outputs[out] = value;
  }

  *vx = outputs[0];
  *vy = outputs[1];
  *vz = outputs[2];
  *strength = outputs[3];
}

void predictFallback(const float *features, float *vx, float *vy, float *vz, float *strength, uint8_t *classId) {
  *vx = clampFloat(features[12] * 0.04f + features[6] * 0.01f, -3.0f, 3.0f);
  *vy = clampFloat(-features[11] * 0.04f - features[5] * 0.01f, -3.0f, 3.0f);
  *vz = max(0.2f, features[4] * 0.02f);
  *strength = sqrt((*vx) * (*vx) + (*vy) * (*vy) + (*vz) * (*vz));

  const uint8_t col = *vx < -0.35f ? 0 : (*vx > 0.35f ? 2 : 1);
  const uint8_t row = *vy > 0.35f ? 0 : (*vy < -0.35f ? 2 : 1);
  *classId = row * 3 + col;
}

float clampFloat(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void printFloat(float value) {
  Serial.print(value, 4);
}
