{
  pkgs ? import <nixpkgs> { },
}:

pkgs.stdenv.mkDerivation {
  pname = "lanmai";
  version = "1.0.0";
  src = ./.;

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.pkg-config
  ];

  buildInputs = [
    # dependencies
    pkgs.libevdev
    pkgs.udev
  ];
}
