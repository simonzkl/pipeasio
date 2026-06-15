{
  description = "PipeASIO Wine ASIO driver backed by PipeWire";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          pipeasio = pkgs.callPackage ./default.nix { };
        in
        {
          default = pipeasio;
          inherit pipeasio;
        }
      );
    };
}
