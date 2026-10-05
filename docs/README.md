## 接続とビルド

| LD2450 | CoreS3 PORT.C |
| --- | --- |
| 5V | V（5V） |
| RX | T（TX / GPIO17） |
| TX | R（RX / GPIO18） |
| GND | G（GND） |

開発環境は Apple Silicon の macOS（`aarch64-darwin`）と、64bit Linux（`x86_64-linux` / `aarch64-linux`）向けに定義している。Nix（flakes 有効）をインストールし、`nix develop` または direnv の `direnv allow` で入る。Intel Mac・32bit Linux は対象外。  
Arduino 公式の `ctags` `5.8-arduino11` を各ホスト向けにNixでビルドし、`$ARDUINO_CTAGS_PATH` を `--build-property` で指定する。Apple SiliconではIntel用配布バイナリを使わずに済む。

Linuxでは、NixpkgsのArduino CLIに付属するFHS互換環境を利用する。Espressif版clangdにも同じ仕組みを使い、Arduinoが取得したコンパイラを `--query-driver` から実行できるようにする。NixOSでもシステム全体の `nix-ld` 設定は不要。実行にはBubblewrapのユーザー名前空間が利用できる環境が必要（制限されたコンテナなどは対象外）。

`firmware/cores3/sketch.yaml` のプロファイルで、ボード用コアとM5Unified・M5GFX のバージョンを固定。  
CoreS3 の Quad PSRAM は `PSRAM=enabled` を明示。

## 開発環境のOS別検証
3構成のNix定義評価、Linux版clangdのSHA-256と共有ライブラリ依存を確認済み。Apple SiliconのmacOSではビルド・テスト9種類・エディタ解析が成功。Linux上の実行・USB書き込みは未検証。各OSで以下を確認する。
```sh
nix develop -c just test
nix develop -c just build
nix develop -c just editor-check
nix develop -c just boards
```

参考：[EspressifのOS別配布物](https://github.com/espressif/llvm-project/releases/tag/esp-21.1.3_20260408)、[Arduino CLIのOS別保存先](https://docs.arduino.cc/arduino-cli/configuration/#default-directories)、[固定済みNixpkgsのArduino CLI Linux実行環境](https://github.com/NixOS/nixpkgs/blob/0a3468a402c449992505b6a9fc5b06580141b750/pkgs/by-name/ar/arduino-cli/package.nix)。

## `justfile`
```sh
just                                  # コマンド一覧
just boards                           # 接続ポート一覧
just test                             # 通信パーサーのテスト（実機不要）
just editor                           # Zed/clangd の解析設定を生成
just editor-check                     # 解析設定を生成し、全ソースの診断を確認
just build                            # コンパイルのみ
just release                          # 公開用 firmware.bin と manifest.json を作成
just upload /dev/cu.usbmodem1101      # ビルドと通常の書き込み
just monitor /dev/cu.usbmodem1101     # USB コンソールから LD2450 の領域設定確認
```

- 書き込み先のポートは `just boards` で確認し、引数に指定。Linuxでは通常 `/dev/ttyACM0` などになるため、例の `/dev/cu.usbmodem1101` を実際のポートに置き換える。USBへのアクセス権限はホスト側で設定する（多くのLinuxでは `dialout` グループ、ディストリビューションにより異なる）。開発コマンドを `sudo` で実行しない。
- どちらの書き込みコマンドも先にビルドを行い、成功した場合だけ書き込み。
- 通常の開発では `just upload` を使う。

以前の保存データもすべて消す必要があるときだけ `upload-erase`
```sh
just upload-erase /dev/cu.usbmodem1101
```

## CI
`.github/workflows/ci.yml` はPR、`main`へのpush、手動実行で動作する。  
- `macos-15`（Apple Silicon）、`ubuntu-24.04`（x86_64）、`ubuntu-24.04-arm`（ARM64）で検証。
- Nixの全3構成を `nix flake check --all-systems --no-build` で評価（Linux x86_64ジョブで1回）。
- 各環境で `nix develop -c just release` を実行し、テスト9種類・ファームウェアのビルド・`firmware.bin`と`manifest.json`の生成を確認。
- 各環境で `nix develop -c just editor-check` を実行し、clangdの診断を確認。
- GitHub Releaseの作成やファイルの公開は行わない。画面・タッチ・センサー・音・OTAの実動作は実機で確認する。
- Ubuntuのユーザー名前空間制限が有効な場合は、CIランナー内でNixのBubblewrapに限定したAppArmorプロファイルを読み込む。
- ActionはコミットSHAで固定し、DependabotがNixとGitHub Actionsの更新PRを週1回作成する。CIのトークン権限は `contents: read`。

参考：[GitHubの標準ランナー](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)、[NixインストールAction](https://github.com/cachix/install-nix-action)、[Ubuntuのユーザー名前空間制限](https://ubuntu.com/blog/ubuntu-23-10-restricted-unprivileged-user-namespaces)。

## Zed の C++ 解析設定
プロジェクトルートで Nix 環境に入り、次を実行する。flake の変更後に direnv が承認を求めた場合は、先に `direnv allow` を実行する。
```sh
just editor
```

生成後はNix環境が有効なターミナルから `zed .` で開く。既に開いている場合は環境を読み直してプロジェクトを開き直す。解析データベースのみを更新した場合は `editor: restart language server` でもよい。ZedのLSPログでは `/usr/bin/env` に `direnv exec . clangd` が渡されていることを確認する。これにより、macOSのログインシェルがPATHの順序を変えても、言語サーバー起動時にプロジェクトのNix環境を適用する。  

新しくcloneしたとき、ソースファイルを追加したとき、ボード設定・ライブラリ・Nix環境を変更したときも `just editor` を再実行する。補完・診断は通常のコード編集に追従する。未開封ファイルの定義ジャンプ・検索は生成時のコピーを参照するため、必要に応じて再生成する。  

- Arduino CLIの `--only-compilation-database` から、実際のincludeパス・マクロ・コンパイルオプションを取得する。生成された `build/editor-arduino/compile_commands.json` を `.clangd` から直接参照する。独自スクリプトによるパス変換は行わない。元ファイルの解析には、clangdが同名のビルド用コピーから推定したコンパイル設定を使う。
- `.clangd` のターゲット・標準ヘッダー・オプション調整は、元ソースと `build/editor-arduino/sketch/` の両方に適用する。バックグラウンド索引はコピーを解析するため、設定が異なると元ソースの単独チェックでは検出できないヘッダー解決の問題が起こり得る。
- ESP32-S3のXtensaを解析するため、Espressif公式clangdをNixでバージョンとSHA-256を固定して取得する。ホスト上のテストは `.clangd` でArduinoのデータベースを参照しない設定にし、Nix環境の `clang++` と `just test` と同じ解析フラグを使う。
- Nixが提供する `clangd` ラッパーを、Zedと `just editor-check` の両方で使う。Zedからは固定の `/usr/bin/env direnv exec . clangd` を経由して起動するため、Zed自身のPATHによるclangdの自動選択に依存しない。標準ヘッダーとターゲットの問い合わせは、Arduinoデータディレクトリ内の `internal/**/bin/xtensa-esp32s3-elf-g++` と、Nixで固定したホストの `clang++` に限定する。Arduino側は実在する単一パスではなく、条件に合う各バージョンのコンパイラを許可する。
- Arduinoデータディレクトリの既定値はmacOSの `$HOME/Library/Arduino15`、Linuxの `$HOME/.arduino15`。変更する場合は `ARDUINO_DIRECTORIES_DATA` をNix/direnv環境に設定し、Arduino CLIとラッパーの両方へ渡す。CLIの設定ファイルだけを変更するとラッパーには反映されない。
- GCC専用のコード生成オプション3つは `.clangd` で解析時だけ除外する。ファームウェアのビルド設定は変えない。
- 解析用の生成物は `build/editor-arduino/`。Zed設定ファイルは生成しない。Git管理する `.zed/settings.json` には、`.ino` をC++として解析するための言語対応付けと、direnv経由の固定起動設定を置く。Nixストアのパスやユーザー名を含む設定の生成は不要。旧設定がある環境では、生成済みのNixストアを指す `lsp.clangd.binary.path` と `--query-driver` 引数を、この固定起動設定に置き換える。
- `just editor-check` は `clangd --check --check-locations=false` でスケッチ・索引用のビルド用コピー・ホストテストのエラー診断を確認する。エラーはターミナルに表示し、検出した時点で失敗する。リファクタリングの自己テストは実行しない。Zedでの起動状態の確認や、`just build` によるファームウェアのコンパイル確認は別途行う。
- `.ino` の解析では `Arduino.h` を読み込むが、Arduinoの自動関数プロトタイプ生成は再現しない。宣言順序はC++として有効に保つ。
- 定義ジャンプやシンボル検索は、未開封のファイルについてビルド用コピーを返すことがある。コピーを編集しても元ソースには反映されず、再生成で上書きされる。修正するときは `firmware/cores3/` の元ファイルを開く。元ファイルを開くと、その情報が索引で優先される。
- テスト・リリース作成の一時作業ディレクトリは、Git管理対象外の `.tmp/` に作成し、終了時に削除する。

参考：[ZedのC++設定](https://zed.dev/docs/languages/cpp)、[clangdのcompile commands](https://clangd.llvm.org/design/compile-commands)、[clangdの索引と開いたファイルの優先順位](https://clangd.llvm.org/design/indexing)、[システムヘッダーの解決](https://clangd.llvm.org/guides/system-headers)、[Espressif clangdの固定リリース](https://github.com/espressif/llvm-project/releases/tag/esp-21.1.3_20260408)。

## 操作と表示
- 起動時は`Display buffers`・`Radar graphics`・`Sensor UART`・`Boot check` の結果を表示する。正常時は `Ready` を約1秒表示してメニューへ進む。ログはUSBシリアルにも出力する。
- `Sensor UART OK` はLD2450の正常なフレームを受信したことを示す。検出対象が0でも成功する。最大2秒で受信できなければ `TIMEOUT` と配線確認メッセージを残し、タップでメニューへ進める。画面バッファ確保や起動確認の失敗時は `FAIL` を表示して停止する（OTA検証中はロールバックが優先される）。起動ログ中のタップはメニュー操作へ引き継がない。
- 起動メニューの `Start` の後、`Sound` 画面で `On`（効果音あり）または `Off`（無音）を選んでトラッカーを開始。`Back` で起動メニューに戻る。選択は保存せず、起動のたびに選ぶ。メニューへ戻るには本体を再起動する。
- CoreS3のスクリーンをタップで表示距離が **6m, 2m, 4m** と切り替え。
- スクリーン下部中央に最も近い対象の距離を表示。
- スクリーン下部の左に表示範囲・相対角度、右に対象番号と検出数・速度をそれぞれ2行で表示。
- 検出点・距離・対象番号・検出数・速度は、波紋の開始時に0.8秒周期でまとめて更新する（アニメーション300ms＋休止500ms）。
- UART受信とジャイロ取得は継続し、目盛りの回転と `REL` はこの周期を待たず更新する。
- 軌跡は表示・記録しない。
- 通信途絶時は周期を待たず点と数値をクリアする。起動時・通信復旧時は、最新データを受信した後の波紋開始時に表示する。
- 起動後は約2秒間静止して、ジャイロの静止補正を行う。（スクリーン長押しでリセット）

| 表示 | 意味 |
| --- | --- |
| `RNG` | トラッカー表示の距離範囲（m） |
| `REL` | ジャイロの基準方向からの相対角度（度） |
| `STILL 2s` | ジャイロの静止補正中（赤文字）。本体を約2秒間静止させる |
| `T1`〜`T3` | 最も近い対象のスロット番号 |
| `T-- N0` | 通信は正常だが、現在の検出対象はない |
| `T-- N--` | 有効なデータを未受信・受信途絶、または起動／復旧後の表示更新待ち |
| `N1`〜`N3` | センサーが報告した検出対象数（厳密な在室人数ではない） |
| `V` | 最も近い対象の速度（cm/s）。対象がない場合は `V --cm/s` |

### 効果音
- `On` は22,050 Hz・16bitモノラルPCMを内蔵スピーカーで再生する。波紋音は800ms周期の開始時、検出音はその120ms後に鳴る。
- 検出音の高さは表示中の `N` と同じ対象数で決まる（1体: 1735 Hz、2体: 2063 Hz、3体: 2454 Hz）。距離による画面外の対象も、`N` と同じく数える。
- 約1.2秒の検出音を2つの再生チャンネルで交互に鳴らし、次の周期でも余韻を切らない。表示の対象数が0になったら検出音を止め、波紋音のみ継続する。
- センサー通信の途絶中、USBからのセンサー設定中、起動メニューでは効果音を停止する。通信復帰後は次の表示更新から再開する。処理が遅れた場合は古い音をまとめて鳴らさない。
- 波形は `firmware/cores3/tracker_sounds.h` に格納し、フラッシュからM5Unifiedの再生タスクに渡す。実行中の波形生成や音声ファイルのダウンロードは不要。`Off` ではスピーカーの再生タスクを停止する。
- 音量は `firmware/cores3/startup_menu.cpp` の `M5.Speaker.setVolume(80)` で指定する（0〜255）。スピーカーの初期化に失敗した場合は `Sound unavailable` と表示し、`Back` から選び直せる。

## Wi-Fi設定とソフトウェア更新
最初の1回は `just upload /dev/cu.usbmodem1101` でUSBから書き込む。全消去は不要。

### Wi-Fiの初回設定
1. 起動メニューで `Wi-Fi setup` をタップする。
2. スマートフォンまたはMacを、画面の SSID `Tracker-xxxxxx` に接続してCoreS3の画面に表示されたパスワードで接続する。
3. ブラウザで **http://192.168.4.1** を開く（HTTPSではない）。「インターネット接続なし」と表示されても、このWi-Fiへの接続を維持する。
4. 自宅などの **2.4GHz** ネットワークを選択、または名前を入力し、`Password` を入力して `Connect & save` を押す。
5. CoreS3に `Wi-Fi saved` と表示されたら完了。約8秒後に設定用Wi-Fiが終了し、起動メニューへ戻る。

- 接続確認は約20秒でタイムアウトする。失敗時は `Try again` から再入力でき、以前保存した設定は上書きしない。
- 失敗時はCoreS3・ブラウザに理由を表示する。例：`Network not found (201)`、`Authentication failed (202)`、`Authentication timed out (204)`、`Security mismatch (210)`。番号はESP32の切断理由コードで、認証タイムアウトだけではパスワード間違いと断定できない。`No IP address received` はルーターへの接続後にIPアドレスを取得できなかったことを示す。
- USBモニターにも失敗理由と接続状態だけを出力する。SSID・パスワードは出力しない。原因確認時は表示されたメッセージと番号を使用する。
- 接続時に設定用Wi-Fiのチャンネルが変わり、ブラウザが切断される場合は同じ `Tracker-xxxxxx` に再接続する。CoreS3側の成功表示でも確認できる。
- 設定用Wi-Fiは毎回新しいパスワードを生成し、最大1台が接続できる。`Back`、保存成功、または開始から5分で停止する。
- 通常のトラッカー動作中はWi-Fiを停止する。起動時の自動接続・自動更新は行わない。
- SSIDとパスワードはCoreS3のNVSにまとめて保存する。公開ファームウェア、GitHub、ログには含めない。NVSの暗号化は本実装では有効化していない。
- ネットワークを変えるときは再び `Wi-Fi setup` を開く。通常のUSB書き込みとOTA更新では保存情報を保持するが、`upload-erase` は消去する。

### 更新
1. `Update` を押すと、保存済みのWi-Fiに接続し、GitHub Releasesの最新リリースを確認する。
2. 新しいバージョンがあれば現在と新しいバージョンを表示する。`Install` で更新、`Back` でキャンセルする。
3. 完了後、自動で再起動する。更新中は電源を接続したままにする。

更新はトラッカーを開始する前のメニュー内だけで行う。Wi-Fi接続・時刻同期・HTTPS通信中は操作を待つ。  
`Up to date` は更新不要、`No release available` は公開リリースまたは `manifest.json` が見つからないことを示す。  

`Update check failed` / `Download failed` の場合は、`just monitor /dev/cu.usbmodem1101` でUSBモニターを開いてから再実行する。`Update:` のログにリクエスト番号・URL長・送信バッファ容量・HTTPステータス、失敗時には処理段階と利用可能なエラーコードを出力する。署名付きURLやWi-Fiの認証情報は出力しない。

インターネットへのHTTPS接続とNTPの時刻同期が必要。認証ページが必要な公衆Wi-Fi・企業向け802.1X認証は対象外。

- ESP32のCA証明書バンドルでHTTPSを検証する。証明書検証を省略する処理はない。
- 機種、パーティション構成、バージョン、ファイルサイズ、SHA-256を確認する。ダウンロード先はこのリポジトリのリリースとGitHubのリリース配信ホストに限定する。
- 更新先は未使用のアプリ領域。検証成功後だけ起動先を切り替え、途中失敗時は現在のファームウェアを維持する。NVSやLD2450の設定は更新対象に含めない。
- OTAの起動確認はディスプレイ用PSRAMバッファの確保とレーダー描画準備の完了まで。OTA後、その前に起動に失敗した場合は対応ブートローダーのロールバックを利用する。UART受信の確認結果は別途表示するが、センサー未接続によるタイムアウトはOTAの起動失敗とはしない。距離の精度や全機能の正常動作を自動判定するものではない。
- 公開ファイルの信頼性はHTTPSとGitHubリポジトリの管理権限に依存する。独立した署名鍵によるファームウェア署名は実装していない。

### リリースファイルを公開する
1. `firmware/cores3/firmware_version.h` の `kFirmwareVersion` を増やす（例：`0.1.0` → `0.1.1`）。
2. `just release` を実行する。テストとコンパイル後、`build/release/firmware.bin` と `build/release/manifest.json` が生成される。
3. 実機でUSB書き込みして確認し、変更をコミットする。
4. GitHubのReleasesで、そのコミットからバージョンに対応するタグ（例：`v0.1.1`）のリリースを作り、上記 **2ファイルだけ** を添付する。通常リリースとして公開し、Latestに指定する。

`just release` 自体はタグの作成・push・リリース公開を行わない。
ファームウェアとmanifestは必ず同じビルドの組み合わせを使う。公開済みバージョンの添付ファイルは差し替えず、新しいバージョンとして公開する。
`merged.bin`、ブートローダー、パーティションイメージ、実機から読み出したフラッシュ全体はOTA用に添付しない。
ボード設定は現在の構成と同じ `app3M_fat9M_16MB`（アプリ3MiB×2）に固定している。

### 実機で確認する項目
- Wi-Fiの正しいパスワード／誤ったパスワード、`Back`、5分経過による設定モード終了。
- 再起動後の接続情報保持、`Start` 後の既存レーダー操作。
- 最新版・更新なし・リリースなし・ネットワーク切断時の表示。
- バージョンを上げた実際のリリースからのOTA、更新後のバージョンとWi-Fi設定・LD2450設定の保持。

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

### 描画負荷の軽減
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
