{ pkgs ? import (builtins.fetchTarball {
    # use nixpkgs channel 25.05
    url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/25.05.tar.gz";
  }) {}
}:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    gnumake
    openocd
    python3Minimal
    picotool
  ];

  buildInputs = with pkgs; [
    gcc-arm-embedded
  ];

  shellHook = ''
    echo "use commands setup, compile and upl"
    # put those in makefile, that's easier in the end
    alias setup='rm -rf build && mkdir build && cd build && cmake ..'
    alias compile='make kassiopeia'
    alias upl='openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program kassiopeia.elf verify reset exit"'
  '';
}
