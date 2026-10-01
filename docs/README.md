## 接続とビルド
![HLK-LD2450](./ld2450.jpg)
![CoreS3](./s3.png)

| LD2450 | CoreS3 PORT.C |
| --- | --- |
| 5V | V（5V） |
| RX | T（TX / GPIO17） |
| TX | R（RX / GPIO18） |
| GND | G（GND） |

Arduino が配布する macOS 用 `ctags` は Intel 用で Apple Silicon ではそのまま実行できないため、このプロジェクトでは Arduino 公式の同じ `5.8-arduino11` ソースを Nix で ARM 向けにビルドし、`$ARDUINO_CTAGS_PATH` にパスを設定して `--build-property` で指定。  

`firmware/cores3_check/sketch.yaml` のプロファイルで、ボード用コアとM5Unified・M5GFX のバージョンを固定。  
CoreS3 の Quad PSRAM は `PSRAM=enabled` を明示。

## `justfile`
```sh
just                                  # コマンド一覧
just boards                           # 接続ポート一覧
just test                             # 通信パーサーのテスト（実機不要）
just build                            # コンパイルのみ
just upload /dev/cu.usbmodem1101      # ビルドと通常の書き込み
just monitor /dev/cu.usbmodem1101     # USB コンソールから LD2450 の領域設定確認
```

- 書き込み先のポートは `just boards` で確認し、引数に指定。
- どちらの書き込みコマンドも先にビルドを行い、成功した場合だけ書き込み。
- 通常の開発では `just upload` を使う。

以前の保存データもすべて消す必要があるときだけ `upload-erase`
```sh
just upload-erase /dev/cu.usbmodem1101
```

## 操作と表示
- CoreS3のスクリーンをタップで表示距離が **6m, 2m, 4m** と切り替え。
- スクリーン下部中央に最も近い対象の距離を大きく表示。
- スクリーン下部の左に表示範囲・相対角度、右に対象番号と検出数・速度をそれぞれ2行で表示。
- 検出点・距離・対象番号・検出数・速度は、波紋の開始時に0.8秒周期でまとめて更新する（アニメーション300ms＋休止500ms）。
- UART受信とジャイロ取得は継続し、目盛りの回転と `REL` はこの周期を待たず更新する。
- 軌跡は表示・記録しない。
- 通信途絶時は周期を待たず点と数値をクリアする。起動時・通信復旧時は、最新データを受信した後の波紋開始時に表示する。
- 起動後は約2秒間静止して、ジャイロの静止補正を行う。（スクリーン長押しでリセット）

| 表示 | 意味 |
| --- | --- |
| `RNG` | レーダー表示の距離範囲（m） |
| `REL` | ジャイロの基準方向からの相対角度（度） |
| `STILL 2s` | ジャイロの静止補正中（赤文字）。本体を約2秒間静止させる |
| `T1`〜`T3` | 最も近い対象のスロット番号 |
| `T-- N0` | 通信は正常だが、現在の検出対象はない |
| `T-- N--` | 有効なデータを未受信・受信途絶、または起動／復旧後の表示更新待ち |
| `N1`〜`N3` | センサーが報告した検出対象数（厳密な在室人数ではない） |
| `V` | 最も近い対象の速度（cm/s）。対象がない場合は `V --cm/s` |

## LD2450
通常のファームウェアを書き込んだ後、近距離領域フィルターをUSBコンソールからCoreS3経由でUART設定する。  
```sh 
just upload /dev/cu.usbmodem1101
just monitor /dev/cu.usbmodem1101
```

モニター内で次のコマンドを入力し、Enterで送信する。（終了はCtrl+C）
| コマンド | 動作 |
| --- | --- |
| `filter status` | センサーから現在のモード・3領域の座標を読む |
| `filter near` | X=-500〜+500mm、Y=0〜500mmの長方形を除外、残り2領域は未使用にする |
| `filter off` | 領域フィルターを無効にする（領域座標は保持） |
| `filter restore` | 最初の `filter near` 実行前のモード・全領域を復元する |

`FILTER OK (readback verified; normal detection resumed)` が表示されれば成功で、失敗時は`filter status` で確認する。

### 描画負荷の削減
- 固定の内外の半円は起動時にアンチエイリアス付きで描画して保存し、回転時も再利用する。
- 完成した画像を前回表示した画像と16×8px単位で比較し、変化した領域だけLCDに転送する。隣接タイルと同じ差分配置の連続行はまとめて転送する。
- 点のぼかしは起動時に27×27pxのアルファマスクを計算し、描画時は背景に合成する。画面全体へのぼかし処理は行わない。

### Bluetoothの無効化・再有効化
このプロジェクトはUARTで検出データを受信するため、LD2450のBluetoothは無効化して使用可能。
上記のUSBモニターで `bluetooth off` を実行すると、設定開始（0x00FF）→Bluetooth無効化（0x00A4、値00 00）→センサー再起動（0x00A3）を行う。
保存・再起動の成功ACKと、その後の有効なUARTデータの受信を確認し、結果を表示する。

| コマンド | 動作 |
| --- | --- |
| `bluetooth off` | Bluetoothを無効化してセンサーを再起動する |
| `bluetooth on` | Bluetoothを再有効化してセンサーを再起動する |

- 設定はLD2450本体に保存され、電源を切っても残る。起動時やファームウェア書き込み時には自動変更しない。
- 無効化後は公式アプリからの接続・設定・OTA更新ができない。必要な場合だけUSBコンソールから `bluetooth on` で戻す。
- 除外領域とUART速度は変更しない。センサー再起動中は検出が一時停止し、処理後はジャイロの補正のため本体を約2秒静止させる。
- 失敗時は状態が不確かな場合があるため、エラー内容を確認する。設定保存に失敗した場合は自動再起動しない。
- 公式通信仕様V1.03にBluetoothの現在値を読む命令はない。表示する成功結果はACKとUART復帰に基づくもので、BLE電波の停止を実測した結果ではない。
- [公式使用教程V1.1・Bluetooth設定手順](https://h.hlktech.com/download/HLK-LD2450-24G/1/HLK-LD2450%E4%BD%BF%E7%94%A8%E6%95%99%E7%A8%8BV1.1.pdf)

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
