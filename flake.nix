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
        pkgs.arduino-cli
        pkgs.just
        pkgs.clang
      ];
      ARDUINO_CTAGS_PATH = "${arduinoCtags}/bin";
    };
  };
}
