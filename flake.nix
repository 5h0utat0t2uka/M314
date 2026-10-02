{
  description = "Arduino development environment for M5Stack CoreS3 and HLK-LD2450";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.arduino-ctags = {
    url = "github:arduino/ctags/5.8-arduino11";
    flake = false;
  };

  outputs = { nixpkgs, arduino-ctags, ... }:
  let
    system = "aarch64-darwin";
    pkgs = nixpkgs.legacyPackages.${system};
    # Upstream clangd cannot parse the ESP32-S3 Xtensa target.
    # Keep Espressif's parser separate from the host compiler used by just test.
    espClangd = pkgs.stdenvNoCC.mkDerivation {
      pname = "esp-clangd";
      version = "21.1.3_20260408";
      src = pkgs.fetchurl {
        url = "https://github.com/espressif/llvm-project/releases/download/esp-21.1.3_20260408/clangd-esp-21.1.3_20260408-aarch64-apple-darwin.tar.xz";
        sha256 = "6dccab9acc9766c90383e9b2585fa1cfabad6125bdc32987a86a02cea598970d";
      };
      installPhase = ''
        mkdir -p "$out"
        cp -R . "$out/"
      '';
      dontFixup = true;
    };
    # Zed and editor-check use the same clangd through the project PATH.
    clangd = pkgs.writeShellScriptBin "clangd" ''
      exec ${espClangd}/bin/clangd \
        --query-driver="''${ARDUINO_DIRECTORIES_DATA:-$HOME/Library/Arduino15}/internal/**/bin/xtensa-esp32s3-elf-g++,${pkgs.clang}/bin/clang++" \
        "$@"
    '';
    # Arduino only distributes an Intel macOS binary for this tool.
    # Build the same Arduino-specific release natively on Apple Silicon.
    arduinoCtags = pkgs.stdenv.mkDerivation {
      pname = "arduino-ctags";
      version = "5.8-arduino11";
      src = arduino-ctags;
      nativeBuildInputs = [ pkgs.autoreconfHook ];
      configureFlags = [ "--enable-tmpdir=/tmp" ];
      # The upstream C sources predate C99.
      # Avoid a collision with the macOS SDK's reserved macro.
      env.CFLAGS = "-std=gnu89";
      postPatch = ''
        substituteInPlace *.[ch] --replace-quiet '__unused__' 'CTAGS_UNUSED'
      '';
    };
  in
  {
    devShells.${system}.default = pkgs.mkShellNoCC {
      packages = [
        clangd
        pkgs.arduino-cli
        pkgs.just
        pkgs.clang
        pkgs.python3
      ];
      ARDUINO_CTAGS_PATH = "${arduinoCtags}/bin";
    };
  };
}
