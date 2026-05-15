// =====================================================================
// CCC2026 電子工作班 - クレーンゲーム コントローラー（プロトタイプ）
// =====================================================================
// 動作確認用の土台コード。本番のゲームロジックは、このコードをベースに
// コントローラー班が実装していく。
//
// 【現在の動作】
//   - スタートボタン押下 → 7セグに "30" 表示
//   - 投下ボタン押下     → 7セグに "0" 表示 + LED が 0.25秒点灯
//   - ジョイスティック    → Serial に方向を出力
// =====================================================================

#include <TM1637Display.h>

// ===== ピン定義 =====
#define CLK 2          // 7セグ TM1637 CLK
#define DIO 3          // 7セグ TM1637 DIO

#define BTN_DROP  4    // 投下ボタン（タクトスイッチ）
#define BTN_START 5    // スタートボタン（係員用）
#define LED_DROP  6    // 投下ボタン照明用LED（220Ω 経由）

#define JOY_X A0       // ジョイスティック HORZ (X軸)
#define JOY_Y A1       // ジョイスティック VERT (Y軸)

// ===== ジョイスティックの閾値 =====
#define THRESHOLD_LOW  300
#define THRESHOLD_HIGH 700

TM1637Display display(CLK, DIO);

void setup() {
  Serial.begin(9600);

  pinMode(BTN_DROP, INPUT_PULLUP);
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(LED_DROP, OUTPUT);

  display.setBrightness(7);
  display.showNumberDec(0);

  digitalWrite(LED_DROP, LOW);    // 起動時は LED 消灯

  Serial.println("Ready");
}

void loop() {
  // ===== ボタン処理 =====
  if (digitalRead(BTN_DROP) == LOW) {
    Serial.println("DROP pressed");
    display.showNumberDec(0);

    // LED を 0.25秒だけ光らせる（簡易実装）
    // TODO: 本番ではゲーム開始時から DROP まで常時点灯
    //       投下後は3秒間点滅 → 消灯、という挙動にする
    digitalWrite(LED_DROP, HIGH);
    delay(250);
    digitalWrite(LED_DROP, LOW);
  }

  if (digitalRead(BTN_START) == LOW) {
    Serial.println("START pressed");
    display.showNumberDec(30);
    delay(200);
  }

  // ===== ジョイスティック処理（物理向きに合わせて反転済み） =====
  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);

  String dir = "";
  if (y > THRESHOLD_HIGH) dir += "UP ";
  if (y < THRESHOLD_LOW)  dir += "DOWN ";
  if (x > THRESHOLD_HIGH) dir += "LEFT ";
  if (x < THRESHOLD_LOW)  dir += "RIGHT ";

  if (dir.length() > 0) {
    Serial.print("Joy: ");
    Serial.print(dir);
    Serial.print("  (x=");
    Serial.print(x);
    Serial.print(", y=");
    Serial.print(y);
    Serial.println(")");
  }

  delay(100);
}