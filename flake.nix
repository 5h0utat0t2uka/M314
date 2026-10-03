{
  description = "Arduino development environment for M5Stack CoreS3 and HLK-LD2450";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.arduino-ctags = {
    url = "github:arduino/ctags/5.8-arduino11";
    flake = false;
  };

  outputs = { nixpkgs, arduino-ctags, ... }:
  let
    clangdSources = {
      aarch64-darwin = {
        target = "aarch64-apple-darwin";
        sha256 = "6dccab9acc9766c90383e9b2585fa1cfabad6125bdc32987a86a02cea598970d";
      };
      x86_64-linux = {
        target = "x86_64-linux-gnu";
        sha256 = "7133e67db271ca96b30ec9f5c34c903eaf5fe10120cfe44f85eb88db7ddbe230";
      };
      aarch64-linux = {
        target = "aarch64-linux-gnu";
        sha256 = "468e6fa03def6bcf0c0469d25b8d8b86212e5a94716d0b3db149f21b77791c54";
      };
    };
  in
  {
    devShells = nixpkgs.lib.genAttrs (builtins.attrNames clangdSources) (system:
    let
      pkgs = nixpkgs.legacyPackages.${system};
      source = clangdSources.${system};
      arduinoDataDir = if pkgs.stdenv.hostPlatform.isDarwin then "Library/Arduino15" else ".arduino15";
      # Upstream clangd cannot parse the ESP32-S3 Xtensa target.
      # Keep Espressif's parser separate from the host compiler used by just test.
      espClangd = pkgs.stdenvNoCC.mkDerivation {
        pname = "esp-clangd";
        version = "21.1.3_20260408";
        src = pkgs.fetchurl {
          url = "https://github.com/espressif/llvm-project/releases/download/esp-21.1.3_20260408/clangd-esp-21.1.3_20260408-${source.target}.tar.xz";
          inherit (source) sha256;
        };
        installPhase = ''
          mkdir -p "$out"
          cp -R . "$out/"
        '';
        dontFixup = true;
      };
      clangdCommand = pkgs.writeShellScript "esp-clangd-command" ''
        exec ${espClangd}/bin/clangd \
          --query-driver="''${ARDUINO_DIRECTORIES_DATA:-$HOME/${arduinoDataDir}}/internal/**/bin/xtensa-esp32s3-elf-g++,${pkgs.clang}/bin/clang++" \
          "$@"
      '';
      # Linux needs the same FHS runtime as nixpkgs' arduino-cli, including for
      # query-driver subprocesses downloaded by Arduino (especially on NixOS).
      clangd = if pkgs.stdenv.hostPlatform.isLinux then pkgs.buildFHSEnv {
        name = "clangd";
        targetPkgs = p: [ p.zlib ];
        runScript = clangdCommand;
      } else pkgs.writeShellScriptBin "clangd" ''
        exec ${clangdCommand} "$@"
      '';
      # Build Arduino's ctags natively on each host. The macOS download is Intel-only.
      arduinoCtags = pkgs.stdenv.mkDerivation {
        pname = "arduino-ctags";
        version = "5.8-arduino11";
        src = arduino-ctags;
        nativeBuildInputs = [ pkgs.autoreconfHook ];
        configureFlags = [ "--enable-tmpdir=/tmp" ];
        # The upstream C sources predate C99.
        env.CFLAGS = "-std=gnu89";
        # Avoid a collision with the macOS SDK's reserved macro.
        postPatch = pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
          substituteInPlace *.[ch] --replace-quiet '__unused__' 'CTAGS_UNUSED'
        '';
      };
    in
    {
      default = pkgs.mkShellNoCC {
        packages = [
          clangd
          pkgs.arduino-cli
          pkgs.just
          pkgs.clang
          pkgs.python3
        ];
        ARDUINO_CTAGS_PATH = "${arduinoCtags}/bin";
      };
    });
  };
}
