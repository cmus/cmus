{
  description = "cmus, built from the local checkout";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };

      # nixpkgs' own cmus, from this checkout. Same plugins and options as
      # the cmus of the system: both read ~/.config/cmus/autosave, and an
      # option one of them lacks (input.mikmod.priority, from the mikmod
      # plugin) stops the other at startup with "no such option".
      cmus = pkgs.cmus.overrideAttrs (old: {
        version = "git";
        src = pkgs.lib.cleanSource ./.; # without result links and .git

        buildInputs = (old.buildInputs or [ ]) ++ [ pkgs.rubberband ];
        configureFlags = (old.configureFlags or [ ]) ++ [ "CONFIG_RUBBERBAND=y" ];

        # The parsers and playback-speed processor.
        doCheck = true;
        checkTarget = "check-lyrics check-cover check-tempo";
      });
    in
    {
      packages.${system} = {
        default = cmus;
        inherit cmus;
      };

      overlays.default = final: prev: {
        cmus = prev.cmus.overrideAttrs (old: {
          version = "git";
          src = final.lib.cleanSource ./.;
          buildInputs = (old.buildInputs or [ ]) ++ [ final.rubberband ];
          configureFlags = (old.configureFlags or [ ]) ++ [ "CONFIG_RUBBERBAND=y" ];
          doCheck = true;
          checkTarget = "check-lyrics check-cover check-tempo";
        });
      };

      checks.${system}.build = cmus;

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ cmus ];
      };

      formatter.${system} = pkgs.nixfmt;
    };
}
