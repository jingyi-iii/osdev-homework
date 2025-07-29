#!/bin/bash
set -euo pipefail

######################################################################
########################### + USER SETTINGS + ########################
######################################################################
tarballs=("binutils" "gdb" "gcc")

declare -A binutils=(
    [fpath]="packages/binutils-2.43.tar.xz"
    [confg]="configure --target=i686-elf --prefix=$HOME/i686-tools --with-sysroot --disable-nls --disable-werror"
    [build]="make -j$(nproc) && make install"
    [envir]="$HOME/i686-tools/bin"
)

declare -A gdb=(
    [fpath]="packages/gdb-15.2.tar.xz"
    [confg]="configure --target=i686-elf --prefix=$HOME/i686-tools --disable-werror"
    [build]="make -j$(nproc) all-gdb && make install-gdb"
    [envir]="$HOME/i686-tools/bin"
)

declare -A gcc=(
    [fpath]="packages/gcc-14.2.0.tar.xz"
    [confg]="configure --target=i686-elf --prefix=$HOME/i686-tools --disable-nls --enable-languages=c,c++ --without-headers"
    [build]="make -j$(nproc) all-gcc && make -j$(nproc) all-target-libgcc && make install-gcc && make install-target-libgcc"
    [envir]="$HOME/i686-tools/bin"
)

prebuilts="gcc g++ make xorriso build-essential bison flex libgmp3-dev libmpc-dev libisl-dev libmpfr-dev texinfo grub-pc-bin qemu-system-i386"
######################################################################
########################### - USER SETTINGS - ########################
######################################################################

check_bash_version() {
    if (( BASH_VERSINFO[0] < 4 )); then
        echo >&2 "ERROR: Bash 4.0 or higher is required. Current version: $BASH_VERSION"
        exit 1
    fi
}

install_prebuilts() {
    echo "Installing system dependencies..."
    if ! sudo apt update; then
        echo >&2 "ERROR: Failed to update package lists"
        exit 1
    fi
    
    if ! sudo apt install -y $prebuilts; then
        echo >&2 "ERROR: Failed to install dependencies"
        exit 1
    fi
}

install_tarballs() {
    local tarball
    for tarball in "${tarballs[@]}"; do
        declare -n curr_tb="$tarball"
        local src_dir="src-$tarball"
        local build_dir="build-$tarball"
        
        echo "=========================================================="
        echo "Building $tarball..."
        echo "=========================================================="
        
        # Check if tarball exists
        if [[ ! -f "${curr_tb[fpath]}" ]]; then
            echo >&2 "ERROR: Tarball not found: ${curr_tb[fpath]}"
            exit 1
        fi
        
        # Clean previous builds
        rm -rf "$src_dir" "$build_dir"
        mkdir -p "$src_dir" "$build_dir"
        
        # Extract tarball
        echo "Extracting ${curr_tb[fpath]}..."
        if ! tar vxf "${curr_tb[fpath]}" -C "$src_dir" --strip-components=1; then
            echo >&2 "ERROR: Failed to extract ${curr_tb[fpath]}"
            exit 1
        fi
        
        # Configure
        echo "Configuring..."
        if ! (cd "$build_dir" && ../"$src_dir"/${curr_tb[confg]}); then
            echo >&2 "ERROR: Configuration failed for $tarball"
            exit 1
        fi
        
        # Build and install
        echo "Building and installing..."
        if ! (cd "$build_dir" && bash -c "${curr_tb[build]}"); then
            echo >&2 "ERROR: Build failed for $tarball"
            exit 1
        fi
        
        # Add to PATH if needed
        if [[ -d "${curr_tb[envir]}" ]]; then
            if ! grep -qFx "export PATH=\"${curr_tb[envir]}:\$PATH\"" ~/.bashrc; then
                echo "Adding ${curr_tb[envir]} to PATH in ~/.bashrc"
                echo "export PATH=\"${curr_tb[envir]}:\$PATH\"" >> ~/.bashrc
            fi
        fi
        
        # Cleanup
        rm -rf "$src_dir" "$build_dir"
        
        echo "$tarball successfully installed!"
        echo
        unset -n curr_tb
    done
}

main() {
    check_bash_version
    install_prebuilts
    install_tarballs
    
    echo "=========================================================="
    echo "All components installed successfully!"
    echo "Please run 'source ~/.bashrc' or restart your terminal"
    echo "to update your PATH environment variable."
    echo "=========================================================="
}

main