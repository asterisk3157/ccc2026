# バーチャル輪投げ プロトタイプ手順

このディレクトリは、輪投げデバイスのデータ収集から Pro Micro 推論までを通すための作業メモです。公開しても問題ない技術情報だけを書いています。

## 構成

| ファイル | 役割 |
|---|---|
| `../../ringtoss-collect.html` | 3x3 ターゲット表示と Web Serial CSV 収集 |
| `../../ringtoss-trajectory.html` | 学習済みモデルとCSVから的ごとの予測軌跡を表示 |
| `../ringtoss_data_collector/ringtoss_data_collector.ino` | ジャイロ3軸の時系列を送る収集用ファーム |
| `../../tools/ringtoss_train.py` | CSV から特徴量抽出、線形モデル学習、Arduino ヘッダ出力 |
| `../ringtoss_inference/ringtoss_inference.ino` | 学習済み係数を使う推論ファーム |
| `../ringtoss_inference/ringtoss_model_coefficients.h` | 係数ヘッダ。学習後に上書きする |

## 1. データ収集ファームを書き込む

Arduino IDE で `prototypes/ringtoss_data_collector/ringtoss_data_collector.ino` を開き、Pro Micro に書き込みます。

必要ライブラリ:

- `MPU6050_tockn`
- `Wire`

シリアルは `115200bps` です。D5 の1ボタンを押している間、次の形式で送信します。

```text
BEGIN,<trial>,<rate_hz>,<baseline_ms>
S,<trial>,<seq>,<t_ms>,<phase>,<gx>,<gy>,<gz>
BASE,<trial>,<samples>,<gx0>,<gy0>,<gz0>
END,<trial>,<samples>,<duration_ms>,<reason>
```

最初の `baseline_ms` は、ボタンを押したまま手前に引く区間です。この区間のジャイロ平均を基準として差し引きます。

## 2. Web 収集ページで CSV を作る

Chrome または Edge で `ringtoss-collect.html` を開きます。Web Serial API は HTTPS または localhost 上で使うのが安定します。

操作:

1. `シリアル接続` で Pro Micro を選ぶ
2. `収集開始` を押す
3. 光ったマスに向けて、ボタンを押したまま手前に引く
4. そのまま前へ振り、振り終わったらボタンを離す
5. 十分に集めたら `CSV保存`

安全のため、デバイスは投げず、手首ストラップを付けます。

## 3. 学習する

CSV を保存したら、Python で係数ヘッダを生成します。ジャイロ3軸の大きさのピークを仮想リリース点として、まずは 9 マス分類と、3D ベクトル + 強さの回帰を同時に学習します。

```bash
python3 tools/ringtoss_train.py path/to/ringtoss-session.csv --header prototypes/ringtoss_inference/ringtoss_model_coefficients.h
```

このスクリプトは外部ライブラリなしで動きます。学習後、同じ場所に `ringtoss_model_coefficients.summary.json` と `ringtoss_model.web.json` も出力します。

## 4. 軌跡を確認する

`ringtoss-trajectory.html` を開き、学習で出力された `ringtoss_model.web.json` と、収集した CSV を読み込みます。的を選ぶと、その的に属する全投について、モデルが予測した `vx,vy,vz,strength` から軌跡を重ねて表示します。

## 5. 推論ファームを書き込む

Arduino IDE で `prototypes/ringtoss_inference/ringtoss_inference.ino` を開き、Pro Micro に書き込みます。

推論結果は次の形式です。

```text
RING,<vx>,<vy>,<vz>,<strength>,<class_id>,<samples>
```

`vx,vy,vz` が輪の初速ベクトル、`strength` が強さ、`class_id` が 3x3 マスの推定値です。`class_id` は左上から右下へ `0..8` です。

## データ収集の目安

最初の疎通確認は各マス 2〜3 回、合計 18〜27 投で十分です。1 人で学習の傾向を見るなら各マス 10 回、合計 90 投を目安にします。Phase 1 以降は、各マスにつき最低 20 回以上、複数人で集めます。最終的な輪っかデバイスのセンサー向きが変わる場合は、必ず最終形で取り直します。
