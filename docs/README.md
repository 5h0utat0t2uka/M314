## 接続とビルド
![HLK-LD2450](./ld2450.jpg)
![CoreS3](./s3.png)

| LD2450 | CoreS3 PORT.C |
| --- | --- |
| 5V | V（5V） |
| RX | T（TX / GPIO17） |
| TX | R（RX / GPIO18） |
| GND | G（GND） |

Arduino が配布する macOS 用 `ctags` は Intel 用で Apple Silicon ではそのまま実行できないため、このプロジェクトでは Arduino 公式の同じ `5.8-arduino11` ソースを Nix で ARM 向けにビルドし、`$ARDUINO_CTAGS_PATH` にパスを設定して `--build-property` で指定  

`firmware/cores3_check/sketch.yaml` のプロファイルで、ボード用コアとM5Unified・M5GFX のバージョンを固定  
CoreS3 の Quad PSRAM は `PSRAM=enabled` を明示

## `justfile`
```sh
just                                  # コマンド一覧
just boards                           # 接続ポート一覧
just test                             # 通信パーサーのテスト（実機不要）
just build                            # コンパイルのみ
just upload /dev/cu.usbmodem1101      # ビルドと通常の書き込み
just monitor /dev/cu.usbmodem1101     # USB コンソールから LD2450 の領域設定確認
```

- 書き込み先のポートは `just boards` で確認し、引数に指定
- どちらの書き込みコマンドも先にビルドを行い、成功した場合だけ書き込み
- 通常の開発では `just upload` を使う

以前の保存データもすべて消す必要があるときだけ `upload-erase`
```sh
just upload-erase /dev/cu.usbmodem1101
```

## 操作と表示
- CoreS3のスクリーンをタップで表示距離が **6m, 2m, 4m** と切り替え
- スクリーン下部中央に最も近い対象の距離を大きく表示
- スクリーン下部の左に表示範囲・相対角度・速度、右に対象番号と検出数・X/Y座標を表示
- 起動後は約2秒間静止して、ジャイロの静止補正を行う（スクリーン長押しでリセット）

| 表示 | 意味 |
| --- | --- |
| `N0` / `NO TARGET` | 通信は正常だが、現在の検出対象はない |
| `N1`〜`N3` | センサーが報告した検出対象数（厳密な在室人数ではない） |
| `X` / `Y` | 各対象の座標（mm） |
| `V` | 各対象の速度（cm/s） |
| `WAITING` | 起動後、まだ有効なデータを受信していない |
| `TIMEOUT` | 最後の有効なデータから1秒以上経過した |

## LD2450
通常のファームウェアを書き込んだ後、近距離領域フィルターをUSBコンソールからCoreS3経由でUART設定する  
```sh 
just upload /dev/cu.usbmodem1101
just monitor /dev/cu.usbmodem1101
```

モニター内で次のコマンドを入力し、Enterで送信する（終了はCtrl+C）
| コマンド | 動作 |
| --- | --- |
| `filter status` | センサーから現在のモード・3領域の座標を読む |
| `filter near` | X=-500〜+500mm、Y=0〜500mmの長方形を除外、残り2領域は未使用にする |
| `filter off` | 領域フィルターを無効にする（領域座標は保持） |
| `filter restore` | 最初の `filter near` 実行前のモード・全領域を復元する |

`FILTER OK (readback verified; normal detection resumed)` が表示されれば成功で、失敗時は`filter status` で確認する

## Ref
- [M314 Motion Tracker](https://avp.fandom.com/wiki/M314_Motion_Tracker)
- [M5GFX 0.2.30 の描画API（drawSmoothLine / fillSmoothCircle）](https://github.com/m5stack/M5GFX/blob/0.2.30/src/lgfx/v1/LGFXBase.hpp)
- [M5Unified IMU API](https://docs.m5stack.com/en/arduino/m5unified/imu_class)
- [M5Unified 0.2.23 の IMU 公式サンプル](https://github.com/m5stack/M5Unified/blob/0.2.23/examples/Basic/Imu/Imu.ino)
- [HLK-LD2450 公式通信仕様 V1.03（2.3節：受信フレーム）](https://h.hlktech.com/download/HLK-LD2450-24G/1/LD2450%20%E4%B8%B2%E5%8F%A3%E9%80%9A%E4%BF%A1%E5%8D%8F%E8%AE%AE%20V1.03.pdf)
- [CoreS3 のピン配置（PORT.C）](https://docs.m5stack.com/en/core/CoreS3#pinmap)
- [M5Stack CoreS3 の Arduino 手順](https://docs.m5stack.com/en/arduino/m5cores3/program)
- [M5Stack ボード用パッケージ一覧](https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json)
- [Arduino CLI ビルドプロファイル仕様](https://github.com/arduino/arduino-cli/blob/master/docs/sketch-project-file.md)
- [Arduino CLI 1.5.1 の FQBN 結合処理](https://github.com/arduino/arduino-cli/blob/v1.5.1/internal/cli/arguments/fqbn.go)
- [Arduino ctags の Apple Silicon 対応状況](https://github.com/arduino/ctags/issues/20)
- [M5Unified 0.2.23](https://github.com/m5stack/M5Unified/releases/tag/0.2.23)
- [M5GFX 0.2.30](https://github.com/m5stack/M5GFX/releases/tag/0.2.30)
