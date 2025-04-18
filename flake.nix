{
  description = "CHAD project flake";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = {
    self,
    nixpkgs,
  }: let
    # System types to support.
    supportedSystems = ["x86_64-linux" "x86_64-darwin" "aarch64-linux" "aarch64-darwin"];

    # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

    # Nixpkgs instantiated for supported system types.
    nixpkgsFor = forAllSystems (system: import nixpkgs {inherit system;});
  in {
    packages = forAllSystems (system: let
      pkgs = nixpkgsFor.${system};
    in {
      prod_server = pkgs.writeShellApplication {
        name = "CHAD_server";
        runtimeInputs = [pkgs.clang pkgs.lld];
        text = ''
          cd ./server/
          cc -o nob nob.c
          ./nob
          ./build/main -url "ws://0.0.0.0:8000"
        '';
      };
    });

    devShells = forAllSystems (system: let
      pkgs = nixpkgsFor.${system};
    in {
      prod = pkgs.mkShell {
        packages = [
          pkgs.clang
          pkgs.lld
        ];
      };

      default = pkgs.mkShell {
        packages = [
          pkgs.clang
          pkgs.clang-tools
          pkgs.lld
          pkgs.go-task
          pkgs.gf

          # libwebsocket deps
          pkgs.cmake
          pkgs.gnumake
          pkgs.openssl
          # pkgs.glibc
          # pkgs.gnused
          # pkgs.triton-llvm
          # pkgs.libwebsockets
        ];
      };
    });
  };
}
