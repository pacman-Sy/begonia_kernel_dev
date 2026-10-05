#!/usr/bin/env bash
#
# build.sh - PoWeR kernel builder for begonia (Redmi Note 8 Pro, MT6785)
#
# Builds the kernel with a pinned Android clang + GCC binutils toolchain and
# packages a flashable AnyKernel3 zip.
#
# Requirements: bash, curl, tar, unzip, zip, cpio, make, python3
#
# Environment overrides:
#   KERNEL_NAME   zip/kernel name (default: MeTh-Kernel-begonia)
#   CLANG_SRC     toolchain flavour: "llvm" for an upstream LLVM release,
#                 "android" for a pinned Android prebuilt (default: llvm)
#   CLANG_VER     with CLANG_SRC=llvm this is an LLVM release such as 21.1.8;
#                 with "android" an Android tag such as clang-r383902
#                 (default: 21.1.8)
#   GCC_VER       android gcc/binutils tag, e.g. android-11.0.0_r1 (default)
#   TC_ROOT       directory where toolchains are stored (default: $HOME/toolchains)
#   OUT_DIR       kernel out directory (default: out)
#   DEFCONFIG     kernel defconfig (default: begonia_apatch_defconfig; use
#                 begonia_user_defconfig for a plain non-APatch build)
#   CLANG_DIR     preinstalled clang prefix dir (bin/clang expected inside)
#   GCC_DIR       preinstalled binutils prefix dir (bin/aarch64-linux-android-* inside)
#   JOBS          build jobs (default: $(nproc))
#   SKIP_CLEAN    set to 1 to reuse an existing out/ tree
#   SKIP_PACKAGE  set to 1 to skip making the flashable zip
#   KEEP_CUSTOM_FLAGS
#                 set to 1 to keep CONFIG_LLVM_POLLY / CONFIG_INLINE_OPTIMIZATION
#                 even if the toolchain does not support the custom -mllvm flags
#                 (only use with a patched/MTK clang that supports them)
#   EXTRA_FLAGS   extra flags appended to both make invocations

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_NAME="${KERNEL_NAME:-MeTh-Kernel-begonia}"
# Toolchain flavour: "llvm" pulls an upstream LLVM release (e.g. 21.1.8),
# "android" pulls a pinned Android prebuilt such as clang-r383902.
CLANG_SRC="${CLANG_SRC:-llvm}"
CLANG_VER="${CLANG_VER:-21.1.8}"
GCC_VER="${GCC_VER:-android-11.0.0_r1}"
TC_ROOT="${TC_ROOT:-$HOME/toolchains}"
OUT_DIR="${OUT_DIR:-out}"
JOBS="${JOBS:-$(nproc)}"
EXTRA_FLAGS="${EXTRA_FLAGS:-}"
DATE="$(date +%Y%m%d-%H%M)"
if [[ "$CLANG_SRC" == "llvm" ]]; then
    CLANG_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANG_VER}/LLVM-${CLANG_VER}-Linux-X64.tar.xz"
    CLANG_ARCHIVE="tar.xz"
    # LLVM=1     : use the kernel's native clang/lld/llvm-* tool selection
    # LLVM_IAS=1 : use clang's integrated assembler. Without this the tree
    #              passes -no-integrated-as (for MTK's patched gas) and clang
    #              shells out to the *host* x86 as, which rejects "-EL".
    LLVM_MAKE_FLAGS="LLVM=1 LLVM_IAS=1"
else
    CLANG_URL="https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/tags/${GCC_VER}/${CLANG_VER}.tar.gz"
    CLANG_ARCHIVE="tar.gz"
    LLVM_MAKE_FLAGS=""
fi
GCC_URL="https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/+archive/refs/tags/${GCC_VER}/aarch64-linux-android-4.9.tar.gz"
AK3_URL="https://github.com/osm0sis/AnyKernel3/archive/refs/heads/master.zip"

ARCH=arm64
CC=clang
CLANG_TRIPLE=aarch64-linux-gnu-
CROSS_COMPILE=aarch64-linux-android-
DEFCONFIG="${DEFCONFIG:-begonia_apatch_defconfig}"

log() { printf '\033[1;32m[*] %s\033[0m\n' "$*"; }

mkdir -p "$TC_ROOT"

setup_clang() {
    local tc_dir="$TC_ROOT/clang-$CLANG_VER"
    if [[ -n "${CLANG_DIR:-}" ]]; then
        TC_CLANG="$CLANG_DIR"
        log "Using preinstalled clang at $TC_CLANG"
    elif [[ -x "$tc_dir/bin/clang" ]]; then
        TC_CLANG="$tc_dir"
        log "Using cached clang at $TC_CLANG"
    else
        log "Downloading clang $CLANG_VER ($CLANG_SRC) ..."
        rm -rf "$tc_dir" "$TC_ROOT/clang.tar.gz" "$TC_ROOT/clang.tar.xz"
        curl -L --fail --retry 3 -o "$TC_ROOT/clang.tar.$CLANG_ARCHIVE" "$CLANG_URL"
        rm -rf "$TC_ROOT/clang-extract"
        mkdir -p "$TC_ROOT/clang-extract"
        if [[ "$CLANG_ARCHIVE" == "tar.xz" ]]; then
            tar -xJf "$TC_ROOT/clang.tar.xz" -C "$TC_ROOT/clang-extract"
        else
            tar -xzf "$TC_ROOT/clang.tar.gz" -C "$TC_ROOT/clang-extract"
        fi
        # Upstream LLVM releases unpack into LLVM-<ver>-Linux-X64/, Android
        # prebuilts unpack flat. Normalise so bin/clang always sits directly
        # under the toolchain root.
        if [[ -x "$TC_ROOT/clang-extract/bin/clang" ]]; then
            mv "$TC_ROOT/clang-extract" "$tc_dir"
        else
            local inner
            inner="$(find "$TC_ROOT/clang-extract" -maxdepth 2 -type d -name bin -printf '%h\n' | head -1)"
            [[ -n "$inner" ]] || { echo "ERROR: no clang binary in archive" >&2; exit 1; }
            mv "$inner" "$tc_dir"
        fi
        rm -rf "$TC_ROOT/clang-extract" "$TC_ROOT/clang.tar.$CLANG_ARCHIVE"
        TC_CLANG="$tc_dir"
    fi
    [[ -x "$TC_CLANG/bin/clang" ]] || { echo "ERROR: clang missing at $TC_CLANG/bin/clang" >&2; exit 1; }
    log "clang in use: $("$TC_CLANG/bin/clang" --version | head -1)"
}

# Build the binutils shim directory for CROSS_COMPILE.
#
# The kernel derives LD/NM/OBJCOPY/etc from CROSS_COMPILE. When the
# compiler is clang the matching assembler, linker and binutils have to
# come from the same LLVM release, otherwise GCC 4.9's 2013-era binutils
# gets handed objects that use newer ELF features and fails to link.
setup_binutils_shim() {
    local prefix="$1" llvm="$2"
    local tc
    for tc in ar nm objcopy objdump ranlib readelf size strings strip addr2line; do
        [[ -x "$llvm/bin/llvm-$tc" ]] || continue
        ln -sf "$llvm/bin/llvm-$tc" "$prefix/$CROSS_COMPILE$tc"
    done
    ln -sf "$llvm/bin/ld.lld" "$prefix/${CROSS_COMPILE}ld"
    log "LLVM binutils shim ready in $prefix (ld -> ld.lld)"
}

setup_gcc() {
    if [[ -n "${GCC_DIR:-}" ]]; then
        TC_GCC="$GCC_DIR"
        log "Using preinstalled binutils at $TC_GCC"
    elif [[ -x "$TC_ROOT/gcc-$GCC_VER/bin/aarch64-linux-android-ld" ]]; then
        TC_GCC="$TC_ROOT/gcc-$GCC_VER"
        log "Using cached binutils at $TC_GCC"
    else
        log "Downloading aarch64-linux-android-4.9 binutils ..."
        curl -L --fail --retry 3 -o "$TC_ROOT/gcc.tar.gz" "$GCC_URL"
        mkdir -p "$TC_ROOT/gcc-$GCC_VER"
        tar -xzf "$TC_ROOT/gcc.tar.gz" -C "$TC_ROOT/gcc-$GCC_VER"
        rm -f "$TC_ROOT/gcc.tar.gz"
        TC_GCC="$TC_ROOT/gcc-$GCC_VER"
    fi
}

setup_anykernel() {
    if [[ -f "$ROOT_DIR/anykernel/.ak3-ok" ]]; then
        AK3_DIR="$ROOT_DIR/anykernel"
        log "Using vendored AnyKernel3 at $AK3_DIR"
        return
    fi
    log "Downloading AnyKernel3 template ..."
    curl -L --fail --retry 3 -o "$TC_ROOT/ak3.zip" "$AK3_URL"
    rm -rf "$ROOT_DIR/anykernel"
    mkdir -p "$ROOT_DIR/anykernel"
    unzip -q "$TC_ROOT/ak3.zip" -d "$TC_ROOT/ak3-tmp"
    cp -r "$TC_ROOT/ak3-tmp"/AnyKernel3-master/. "$ROOT_DIR/anykernel/"
    rm -rf "$TC_ROOT/ak3-tmp" "$TC_ROOT/ak3.zip"
    touch "$ROOT_DIR/anykernel/.ak3-ok"
    AK3_DIR="$ROOT_DIR/anykernel"
}

prepare_config() {
    local test_src="$TC_ROOT/.tc-test.c"
    printf 'int x;\n' > "$test_src"
    [[ -f "$ROOT_DIR/$OUT_DIR/.config" ]] || {
        echo "ERROR: $OUT_DIR/.config missing; defconfig step did not run" >&2
        return 1
    }
    # Upstream LLVM releases (clang 21) do not carry MediaTek's patched Polly
    # or the custom unroll/inline threshold patches, so probe before use and
    # drop the options that would otherwise fail the build.
    if [[ "${KEEP_CUSTOM_FLAGS:-0}" == "1" ]]; then
        log "KEEP_CUSTOM_FLAGS=1 - keeping custom -mllvm flags"
        return
    fi
    if ! "$TC_CLANG/bin/clang" --target=aarch64-linux-gnu \
        -mllvm -polly -mllvm -polly-postopts=1 -mllvm -polly-ast-use-context \
        -mllvm -polly-detect-keep-going -mllvm -polly-vectorizer=stripmine \
        -mllvm -polly-invariant-load-hoisting -c "$test_src" -o /dev/null 2>/dev/null; then
        log "Toolchain lacks patched LLVM Polly - disabling CONFIG_LLVM_POLLY"
        ./scripts/config --file "$ROOT_DIR/$OUT_DIR/.config" --disable LLVM_POLLY
    fi
    if ! "$TC_CLANG/bin/clang" --target=aarch64-linux-gnu \
        -mllvm -unroll-threshold=1200 -mllvm -unroll-threshold=900 \
        -mllvm -inline-threshold=2000 -mllvm -inline-threshold=1300 \
        -c "$test_src" -o /dev/null 2>/dev/null; then
        log "Toolchain rejects repeated -mllvm thresholds - disabling CONFIG_INLINE_OPTIMIZATION"
        ./scripts/config --file "$ROOT_DIR/$OUT_DIR/.config" --disable INLINE_OPTIMIZATION
    fi
    make O="$OUT_DIR" ARCH="$ARCH" CC="$CC" \
        CLANG_TRIPLE="$CLANG_TRIPLE" CROSS_COMPILE="$CROSS_COMPILE" \
        $EXTRA_FLAGS $LLVM_MAKE_FLAGS olddefconfig
}

build_kernel() {
    log "Building kernel (defconfig: $DEFCONFIG, jobs: $JOBS)"
    # With an upstream LLVM release, binutils come from the same release via
    # the shim, so do not put the 2013-era Android GCC 4.9 binutils on PATH
    # where they would shadow ld.lld / llvm-*.
    if [[ "$CLANG_SRC" == "llvm" ]]; then
        setup_binutils_shim "$TC_CLANG/bin" "$TC_CLANG"
        export PATH="$TC_CLANG/bin:$PATH"
    else
        export PATH="$TC_CLANG/bin:$TC_GCC/bin:$PATH"
    fi
    log "ld resolves to: $(command -v ${CROSS_COMPILE}ld || echo '<not found>')"
    local bcc="$CC"
    if command -v ccache >/dev/null 2>&1 && [[ "${CCACHE:-1}" == "1" ]]; then
        bcc="ccache $CC"
        log "Using ccache"
    fi
    mkdir -p "$ROOT_DIR/$OUT_DIR"
    cd "$ROOT_DIR"
    make O="$OUT_DIR" ARCH="$ARCH" CC="$bcc" \
        CLANG_TRIPLE="$CLANG_TRIPLE" CROSS_COMPILE="$CROSS_COMPILE" \
        $EXTRA_FLAGS $LLVM_MAKE_FLAGS "$DEFCONFIG"
    prepare_config
    make O="$OUT_DIR" ARCH="$ARCH" CC="$bcc" \
        CLANG_TRIPLE="$CLANG_TRIPLE" CROSS_COMPILE="$CROSS_COMPILE" \
        $EXTRA_FLAGS $LLVM_MAKE_FLAGS -j"$JOBS"
    [[ -f "$OUT_DIR/arch/arm64/boot/Image.gz-dtb" ]] || {
        echo "ERROR: Image.gz-dtb not produced" >&2
        exit 1
    }
}

package_zip() {
    if [[ "${SKIP_PACKAGE:-0}" == "1" ]]; then
        log "Skipping zip packaging"
        return
    fi
    local stage="$ROOT_DIR/$OUT_DIR/anykernel"
    rm -rf "$stage"
    mkdir -p "$stage"
    cp -r "$AK3_DIR/." "$stage/"
    cp "$ROOT_DIR/$OUT_DIR/arch/arm64/boot/Image.gz-dtb" "$stage/Image.gz-dtb"
    cat > "$stage/anykernel.sh" <<EOF
# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
properties() { '
kernel.string=$KERNEL_NAME for Redmi Note 8 Pro (begonia) [APatch]
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=0
device.name1=begonia
device.name2=begonia_in
device.name3=begoniain
device.name4=
supported.versions=
supported.patchlevels=
'; } # end properties

## shell variables
BLOCK=/dev/block/by-name/boot;
IS_SLOT_DEVICE=0;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh;

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
chmod -R 750 \$RAMDISK/*;
chown -R root:root \$RAMDISK/*;

## AnyKernel install
dump_boot;

write_boot;

## end install
EOF
    local zip="$ROOT_DIR/$KERNEL_NAME-$DATE.zip"
    log "Creating flashable zip: $zip"
    (cd "$stage" && zip -r9 "$zip" . -x '*.git*')
    echo "ZIP_PATH=$zip" > "$ROOT_DIR/.zip-path"
}

setup_clang
if [[ "$CLANG_SRC" == "llvm" ]]; then
    # ld/NM/OBJCOPY come from the same LLVM release via the shim, so the
    # 2013-era Android GCC 4.9 binutils are not needed.
    TC_GCC="$TC_CLANG"
else
    setup_gcc
fi
setup_anykernel
build_kernel
package_zip

log "Done. Kernel: $OUT_DIR/arch/arm64/boot/Image.gz-dtb"
if [[ -f "$ROOT_DIR/.zip-path" ]]; then
    log "Flashable zip: $(cat "$ROOT_DIR/.zip-path")"
fi
