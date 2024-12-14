#!/bin/bash

initialize_env() {
    export PREFIX="$HOME/i686-tools"
    export TARGET=i686-elf
    export PATH="$PREFIX/bin:$PATH"
}

install_prebuilt_packages() {
    echo "Installing the prebuilt packages..."
    sudo apt update
    sudo apt install -y xorriso build-essential bison flex libgmp3-dev libmpc-dev libisl-dev libmpfr-dev texinfo grub-pc-bin
}

decompress_tool_packages() {
    tar vxf packages/binutils*
    tar vxf packages/gdb*
    tar vxf packages/gcc*
}

build_tool_packages() {
    echo "Building binutils..."
    mkdir -p build-binutils
    cd build-binutils
    ../binutils*/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
    make -j12
    echo "Installing binutils..."
    make install
    cd ..

    echo "Building gdb..."
    mkdir -p build-gdb
    cd build-gdb
    ../gdb*/configure --target=$TARGET --prefix="$PREFIX" --disable-werror
    make -j12 all-gdb
    echo "Installing gdb..."
    make install-gdb
    cd ..

    echo "Building gcc..."
    mkdir -p build-gcc
    cd build-gcc
    ../gcc*/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
    make -j12 all-gcc
    make -j12 all-target-libgcc
    echo "Installing gcc..."
    make install-gcc
    make install-target-libgcc
    cd ..
}

clean() {
    echo "Cleaning..."
    rm -rf build* binutils* gdb* gcc*
}

main() {
    initialize_env
    install_prebuilt_packages
    decompress_tool_packages
    build_tool_packages
    clean
}

main
