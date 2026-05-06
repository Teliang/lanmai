{
  pkgs ? import <nixpkgs> { },
  fetchFromGitHub,
}:

pkgs.stdenv.mkDerivation {
  pname = "lanmai";
  version = "1.0.0";
  src = ./.;

  # src = fetchFromGitHub {
  #   owner = "teliang";
  #   repo = "lanmai";
  #   rev = "master";
  #   sha256 = "sha256-O1MOQTA/m3j8u3AYh02CkcPDTWM0SH3kLzklxrFFuMg=";
  # };

  # TODO
  # configureFlags = [
  #   "--sysconfdir=/etc"
  # ];

  # systemd.services.lanmai.serviceConfig = {
  #   StateDirectory = "lanmai";
  #   StateDirectoryMode = "0750";
  #   ConfigurationDirectory = "lanmai";
  #   ConfigurationDirectoryMode = "0750";
  # };

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.pkg-config
  ];

  buildInputs = [
    # dependencies
    pkgs.libevdev
    pkgs.udev
  ];

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp lanmai $out/bin
    runHook postInstall
  '';

  postInstall = ''
    mkdir -p $out/lib/systemd/system
    substitute ../etc/lanmai.service $out/lib/systemd/system/lanmai.service \
    --replace /usr $out
  '';
}
