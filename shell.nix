let
  sources = import ./npins;
  pkgs = import sources.nixpkgs { };
in
pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
  nativeBuildInputs = [
    pkgs.catch2_3
    pkgs.clang-tools
    pkgs.cmake
    pkgs.llvmPackages.llvm # for llvm-symbolizer (asan output)
    pkgs.ninja
  ];
}
