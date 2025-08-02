{ pkgs ? import <nixpkgs> {} }:

let
  # the running kernel, packaged together with its build tree
  kernel = pkgs.linuxPackages.kernel;
in
pkgs.stdenv.mkDerivation {
  pname   = "myramfs";
  version = "0.1";
  src     = ./.; 

  # everything the out-of-tree module build needs
  nativeBuildInputs = [
    kernel.dev        # full kernel build directory
    pkgs.kmod         # depmod, insmod … for the install phase
  ];

  # point Kbuild at the correct kernel source tree
  makeFlags = [
    "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
  ];

  installPhase = ''
    mkdir -p $out/lib/modules/${kernel.modDirVersion}/extra
    cp myramfs.ko $out/lib/modules/${kernel.modDirVersion}/extra/
  '';
}
