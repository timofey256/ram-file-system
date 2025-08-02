{ pkgs ? import <nixpkgs> {} }:

let
  # the running kernel, packaged together with its build tree
  kernel = pkgs.linuxPackages.kernel;
in
pkgs.stdenv.mkDerivation {
  pname   = "ramfs-custom";
  version = "0.1";
  src     = ./.;                           # Makefile + ramfs_mmap.c live here

  # everything the out-of-tree module build needs
  nativeBuildInputs = [
    kernel.dev        # full kernel build directory
    pkgs.kmod         # depmod, insmod … for the install phase
  ];

  # point Kbuild at the correct kernel source tree
  makeFlags = [
    "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
  ];

  # drop the .ko into the proper versioned directory
  installPhase = ''
    mkdir -p $out/lib/modules/${kernel.modDirVersion}/extra
    cp ramfs_custom.ko $out/lib/modules/${kernel.modDirVersion}/extra/
  '';
}
