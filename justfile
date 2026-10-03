set positional-arguments

default:
  @just --list

boards:
  arduino-cli board list

# Zed/clangd の解析設定を実際の Arduino ビルド情報から生成
editor:
  arduino-cli compile --profile cores3 --only-compilation-database \
    --build-path build/editor-arduino \
    --build-property "tools.ctags.path=$ARDUINO_CTAGS_PATH" \
    firmware/cores3

# エディタと同じ clangd でスケッチとホストテストのエラー診断を検証
editor-check: editor
  #!/usr/bin/env bash
  set -euo pipefail
  for source in firmware/cores3/*.ino firmware/cores3/*.cpp tests/*.cpp; do
    echo "Checking $source"
    clangd --check="$source" --check-locations=false --log=error
  done

# ホスト上で通信パーサー・相対回転・画面差分を検証（実機不要）
test:
  #!/usr/bin/env bash
  set -euo pipefail
  mkdir -p .tmp
  test_dir="$(mktemp -d "$PWD/.tmp/test.XXXXXX")"
  trap 'rm -rf "$test_dir"' EXIT
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/ld2450_test.cpp -o "$test_dir/ld2450_test"
  "$test_dir/ld2450_test"
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/relative_heading_test.cpp -o "$test_dir/relative_heading_test"
  "$test_dir/relative_heading_test"
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/target_trail_test.cpp -o "$test_dir/target_trail_test"
  "$test_dir/target_trail_test"
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/region_filter_test.cpp -o "$test_dir/region_filter_test"
  "$test_dir/region_filter_test"
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/display_diff_test.cpp -o "$test_dir/display_diff_test"
  "$test_dir/display_diff_test"
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/scan_snapshot_test.cpp -o "$test_dir/scan_snapshot_test"
  "$test_dir/scan_snapshot_test"
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/sound_cues_test.cpp -o "$test_dir/sound_cues_test"
  "$test_dir/sound_cues_test"
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/wifi_settings_test.cpp -o "$test_dir/wifi_settings_test"
  "$test_dir/wifi_settings_test"
  clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    tests/release_info_test.cpp -o "$test_dir/release_info_test"
  "$test_dir/release_info_test"

# USB コンソールから LD2450 の領域設定・解除・確認（Ctrl+C で終了）
monitor port:
  arduino-cli monitor --port "$1" --config baudrate=115200

# 固定した依存物でコンパイル
build:
  arduino-cli compile --profile cores3 \
    --build-property "tools.ctags.path=$ARDUINO_CTAGS_PATH" \
    firmware/cores3

# 公開用のアプリと更新情報を build/release に生成（公開操作は行わない）
release: test
  #!/usr/bin/env bash
  set -euo pipefail
  mkdir -p .tmp
  release_dir="$(mktemp -d "$PWD/.tmp/release.XXXXXX")"
  trap 'rm -rf "$release_dir"' EXIT
  arduino-cli compile --profile cores3 \
    --build-property "tools.ctags.path=$ARDUINO_CTAGS_PATH" \
    --output-dir "$release_dir" firmware/cores3
  python3 scripts/package_release.py "$release_dir/cores3.ino.bin" build/release

# ビルド後、全消去せずに書き込み
upload port: build
  arduino-cli upload --profile cores3 \
    --port "$1" \
    firmware/cores3

# ビルド後、本体フラッシュ内の全データを消去して書き込み
upload-erase port: build
  arduino-cli upload --profile cores3 \
    --fqbn m5stack:esp32:m5stack_cores3:PSRAM=enabled,PartitionScheme=app3M_fat9M_16MB,EraseFlash=all \
    --port "$1" \
    firmware/cores3
