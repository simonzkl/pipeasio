{
  lib,
  stdenv,
  bash,
  cmake,
  file,
  makeWrapper,
  ninja,
  pipewire,
  pkg-config,
  pkgsCross,
  qt6,
  wineWow64Packages,
}:

let
  wine = wineWow64Packages.stable;
  mingw32 = pkgsCross.mingw32.stdenv.cc;
  sourceRootDirs = [
    "cmake"
    "docs"
    "gui"
    "include"
    "src"
    "tests"
  ];
  sourceRootFiles = [
    "CMakeLists.txt"
    "COPYING"
    "README.md"
    "pipeasio-register"
    "pipeasio.dll.spec"
  ];
in
stdenv.mkDerivation (finalAttrs: {
  pname = "pipeasio";
  version = "1.0.0-unstable-2026-06-15";

  src = lib.cleanSourceWith {
    src = ./.;
    filter =
      path: type:
      let
        root = toString ./.;
        rel = lib.removePrefix "${root}/" (toString path);
        top = builtins.head (lib.splitString "/" rel);
      in
      (toString path == root) || lib.elem rel sourceRootFiles || lib.elem top sourceRootDirs;
  };

  nativeBuildInputs = [
    bash
    cmake
    file
    makeWrapper
    mingw32
    ninja
    pkg-config
    qt6.wrapQtAppsHook
    wine
  ];

  buildInputs = [
    pipewire
    qt6.qtbase
  ];

  cmakeFlags = [
    "-DBUILD_SETTINGS_PANEL=ON"
    "-DBUILD_TESTS=ON"
    "-DBUILD_WOW64_32=ON"
    "-DWINE_INCLUDE_DIRS=${wine}/include/wine;${wine}/include/wine/windows"
    "-DWINE_LIB_ROOT=${wine}/lib/wine"
  ];

  doCheck = true;
  dontWrapQtApps = true;

  postFixup = ''
    wrapQtApp "$out/bin/pipeasio-settings"
    wrapProgram "$out/bin/pipeasio-register" \
      --set-default PIPEASIO_PREFIX "$out"
  '';

  meta = {
    description = "ASIO driver for Wine backed by PipeWire";
    homepage = "https://github.com/M0n7y5/pipeasio";
    license = lib.licenses.gpl3Plus;
    mainProgram = "pipeasio-register";
    platforms = [ "x86_64-linux" ];
  };
})
