set positional-arguments

default:
  @just --list

boards:
  arduino-cli board list

# Mac 上で通信パーサー・相対回転を検証（実機不要）
test:
  #!/usr/bin/env bash
  set -euo pipefail
  test_dir="$(mktemp -d)"
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

# USB コンソールから LD2450 の領域設定・解除・確認（Ctrl+C で終了）
monitor port:
  arduino-cli monitor --port "$1" --config baudrate=115200

# 固定した依存物でコンパイル
build:
  arduino-cli compile --profile cores3 \
    --build-property "tools.ctags.path=$ARDUINO_CTAGS_PATH" \
    firmware/cores3_check

# ビルド後、全消去せずに書き込み
upload port: build
  arduino-cli upload --profile cores3 \
    --port "$1" \
    firmware/cores3_check

# ビルド後、本体フラッシュ内の全データを消去して書き込み
upload-erase port: build
  arduino-cli upload --profile cores3 \
    --fqbn m5stack:esp32:m5stack_cores3:PSRAM=enabled,EraseFlash=all \
    --port "$1" \
    firmware/cores3_check
