{
  description = "cmus, built from the local checkout";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };

      cmus = pkgs.stdenv.mkDerivation {
        pname = "cmus";
        version = "git";

        src = ./.;

        nativeBuildInputs = [ pkgs.pkg-config ];

        buildInputs = with pkgs; [
          ncurses
          alsa-lib
          libpulseaudio
          systemd
          ffmpeg
          flac
          libmad
          libmodplug
          libmpcdec
          libvorbis
          wavpack
          opusfile
          libcddb
          libcdio
          libcdio-paranoia
          libcue
        ];

        configureFlags = [
          "CONFIG_ALSA=y"
          "CONFIG_PULSE=y"
          "CONFIG_MPRIS=y"
          "CONFIG_FFMPEG=y"
          "CONFIG_FLAC=y"
          "CONFIG_MAD=y"
          "CONFIG_MODPLUG=y"
          "CONFIG_MPC=y"
          "CONFIG_VORBIS=y"
          "CONFIG_WAVPACK=y"
          "CONFIG_OPUS=y"
          "CONFIG_CDDB=y"
          "CONFIG_CDIO=y"
          "CONFIG_CUE=y"
        ];

        # cmus expects the compiler to perform the final link.
        makeFlags = [ "LD=$(CC)" ];

        meta = with pkgs.lib; {
          description = "Small, fast console music player";
          homepage = "https://cmus.github.io/";
          license = licenses.gpl2;
          platforms = platforms.linux;
        };
      };
    in
    {
      packages.${system} = {
        default = cmus;
        cmus = cmus;
      };

      checks.${system}.build = cmus;

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ cmus ];
      };
    };
}
