#!/bin/bash
# Build script for Begonia Kernel with KernelSU and SUSFS support
# This script builds the kernel for Xiaomi Begonia (MTK MT6785/Serve)

set -e

# Configuration
ARCH=${ARCH:-arm64}
SUBARCH=${SUBARCH:-arm64}
CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}
DEFCONFIG=${DEFCONFIG:-begonia_user_defconfig}
BUILD_DIR=${BUILD_DIR:-out}
JOBS=${JOBS:-$(nproc)}
MODULES_INSTALL_DIR=${MODULES_INSTALL_DIR:-modules}
ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check dependencies
check_dependencies() {
    log_info "Checking dependencies..."
    
    local deps=("make" "gcc" "${CROSS_COMPILE}gcc" "bison" "flex" "libssl-dev" "libelf-dev")
    
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            if [[ "$dep" == "${CROSS_COMPILE}*" ]]; then
                log_error "Cross-compiler ${CROSS_COMPILE}gcc not found!"
                log_info "Install with: sudo apt install gcc-aarch64-linux-gnu"
                exit 1
            fi
            log_warn "Dependency $dep not found, attempting to continue..."
        fi
    done
    
    log_info "Dependencies checked."
}

# Prepare the build environment
prepare() {
    log_info "Preparing build environment..."
    
    # Initialize and update submodules
    if [ -f .gitmodules ]; then
        log_info "Initializing git submodules..."
        git submodule update --init --recursive KernelSU
    fi

    # Backport fixes onto the KernelSU-Next checkout (kept pristine in git).
    # 1. syscall_fn_t: KSUN upstream only defines it for x86_64; arm64's
    #    sys_call_table is `void * const []`. Entries are invoked as
    #    table[nr](regs), so the element type must be a function
    #    pointer taking const struct pt_regs *.
    # 2. linux/pgtable.h: split out of linux/mm.h in 5.x; on 4.14 the
    #    contents still live in asm/pgtable.h (pulled in via linux/mm.h).
    python3 - <<'PY'
    p = 'KernelSU/kernel/hook/syscall_hook.h'
    t = open(p).read()
    old = '#if defined(__x86_64__)\ntypedef sys_call_ptr_t syscall_fn_t;\n#endif'
    new = ('#if defined(__x86_64__)\ntypedef sys_call_ptr_t syscall_fn_t;\n'
           '#elif defined(__aarch64__)\n'
           'typedef long (*syscall_fn_t)(const struct pt_regs *regs);\n'
           '#else\n'
           'typedef long (*syscall_fn_t)(const struct pt_regs *regs);\n'
           '#endif')
    if old in t:
        open(p, 'w').write(t.replace(old, new))
        print('syscall_fn_t backport applied')
    else:
        print('syscall_fn_t backport already present or upstream fixed - skipping')

    p = 'KernelSU/kernel/feature/sucompat.c'
    t = open(p).read()
    old = '#include <linux/pgtable.h>\n'
    new = '#include <linux/version.h>\n#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)\n#include <linux/pgtable.h>\n#endif\n'
    if old in t:
        open(p, 'w').write(t.replace(old, new))
        print('pgtable.h backport applied')
    else:
        print('pgtable.h backport already present or upstream fixed - skipping')
PY

    
    # Clean previous build
    if [ -d "$BUILD_DIR" ]; then
        log_info "Cleaning previous build..."
        make O="$BUILD_DIR" mrproper
    fi
    
    log_info "Build environment prepared."
}

# Configure the kernel
configure() {
    log_info "Configuring kernel with $DEFCONFIG..."
    
    # Copy defconfig
    if [ -f "arch/arm64/configs/$DEFCONFIG" ]; then
        cp "arch/arm64/configs/$DEFCONFIG" "$BUILD_DIR/.config"
    else
        log_error "Defconfig $DEFCONFIG not found!"
        exit 1
    fi
    
    # Ensure the KernelSU/SUSFS prerequisites are present. Exact-match greps only:
    # a substring grep is fooled by lines like CONFIG_KSU_SUSFS=yFOO=y, and if a
    # dependency is missing (KSU needs KPROBES && EXT4_FS) Kconfig silently drops
    # these symbols -> a kernel with no KernelSU and no SUSFS.
    local ksu_cfg="$BUILD_DIR/.config"
    for sym in CONFIG_MODULES=y CONFIG_KPROBES=y CONFIG_KALLSYMS=y CONFIG_KALLSYMS_ALL=y \
               CONFIG_EXT4_FS=y CONFIG_KSU=y CONFIG_KSU_SUSFS=y \
               CONFIG_KSU_SUSFS_SUS_PATH=y CONFIG_KSU_SUSFS_SUS_MOUNT=y \
               CONFIG_KSU_SUSFS_SUS_KSTAT=y CONFIG_KSU_SUSFS_TRY_UMOUNT=y \
               CONFIG_KSU_SUSFS_SPOOF_UNAME=y CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG=y \
               CONFIG_KSU_SUSFS_OPEN_REDIRECT=y CONFIG_KSU_SUSFS_ENABLE_LOG=y; do
        if ! grep -qx "$sym" "$ksu_cfg"; then
            log_info "Enabling $sym"
            echo "$sym" >> "$ksu_cfg"
        fi
    done
    
    # Run olddefconfig to resolve any dependencies
    log_info "Running olddefconfig..."
    make O="$BUILD_DIR" ARCH=$ARCH SUBARCH=$SUBARCH CROSS_COMPILE=$CROSS_COMPILE olddefconfig

    # Fail loudly if Kconfig dropped any of them after resolution
    for sym in CONFIG_MODULES=y CONFIG_KPROBES=y CONFIG_KALLSYMS_ALL=y CONFIG_EXT4_FS=y \
               CONFIG_KSU=y CONFIG_KSU_SUSFS=y CONFIG_KSU_SUSFS_SUS_PATH=y \
               CONFIG_KSU_SUSFS_SUS_MOUNT=y CONFIG_KSU_SUSFS_SUS_KSTAT=y \
               CONFIG_KSU_SUSFS_TRY_UMOUNT=y CONFIG_KSU_SUSFS_SPOOF_UNAME=y \
               CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG=y \
               CONFIG_KSU_SUSFS_OPEN_REDIRECT=y; do
        if ! grep -qx "$sym" "$ksu_cfg"; then
            log_error "$sym is missing from $ksu_cfg after olddefconfig - KernelSU/SUSFS would not be built"
            exit 1
        fi
    done
    
    log_info "Configuration complete."
}

# Build the kernel
build() {
    log_info "Building kernel with $JOBS jobs..."
    log_info "ARCH=$ARCH SUBARCH=$SUBARCH CROSS_COMPILE=$CROSS_COMPILE"
    
    # Build the kernel
    # NOTE: begonia's stock boot image carries Image.gz-dtb, i.e. Image.gz with the
    # mediatek/mt6785 dtb appended (CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE). bzImage is
    # an x86 target and does not exist on arm64.
    make O="$BUILD_DIR" \
         ARCH=$ARCH \
         SUBARCH=$SUBARCH \
         CROSS_COMPILE=$CROSS_COMPILE \
         -j$JOBS \
         dtbs \
         Image.gz-dtb \
         modules
    
    # Fail loudly if KernelSU/SUSFS are not actually linked into the kernel
    local map="$BUILD_DIR/System.map"
    if [ -f "$map" ]; then
        local ksu_count susfs_count
        ksu_count=$(grep -c ' ksu_' "$map" || true)
        susfs_count=$(grep -c 'susfs' "$map" || true)
        log_info "KernelSU symbols: $ksu_count, SUSFS symbols: $susfs_count"
        if [ "${ksu_count:-0}" -lt 5 ] || [ "${susfs_count:-0}" -lt 5 ]; then
            log_error "KernelSU/SUSFS symbols missing from $map - the kernel would boot without root"
            exit 1
        fi
    fi
    
    log_info "Build complete!"
}

# Package the output
package() {
    log_info "Packaging build artifacts..."
    
    local package_name="begonia-kernel-$(date +%Y%m%d-%H%M%S)"
    local package_dir="build_output/$package_name"
    
    mkdir -p "$package_dir"
    
    # Copy kernel image (begonia boots Image.gz-dtb: kernel.gz + appended mt6785 dtb)
    if [ -f "$BUILD_DIR/arch/arm64/boot/Image.gz-dtb" ]; then
        cp "$BUILD_DIR/arch/arm64/boot/Image.gz-dtb" "$package_dir/"
        log_info "Copied Image.gz-dtb"
    fi
    
    if [ -f "$BUILD_DIR/arch/arm64/boot/Image.gz" ]; then
        cp "$BUILD_DIR/arch/arm64/boot/Image.gz" "$package_dir/"
        log_info "Copied Image.gz"
    fi
    
    # Copy DTBs
    if [ -d "$BUILD_DIR/arch/arm64/boot/dts" ]; then
        cp -r "$BUILD_DIR/arch/arm64/boot/dts" "$package_dir/"
        log_info "Copied DTBs"
    fi
    
    # Copy modules
    if [ -d "$BUILD_DIR/modules" ]; then
        cp -r "$BUILD_DIR/modules" "$package_dir/"
        log_info "Copied modules"
    fi
    
    # Create a manifest
    cat > "$package_dir/MANIFEST.txt" << EOF
Kernel Build Manifest
=====================
Date: $(date)
Branch: $(git rev-parse --abbrev-ref HEAD)
Commit: $(git rev-parse HEAD)
KernelSU Commit: $(cd KernelSU && git rev-parse HEAD 2>/dev/null || echo "N/A")
Config: $DEFCONFIG
ARCH: $ARCH
CROSS_COMPILE: $CROSS_COMPILE
EOF
    
    log_info "Package created: $package_dir"
    
    # Create tarball
    cd build_output
    tar -czf "${package_name}.tar.gz" "$package_name"
    cd ..
    
    log_info "Tarball created: build_output/${package_name}.tar.gz"
}

# Package a TWRP flashable AnyKernel3 zip
package_anykernel() {
    log_info "Packaging TWRP flashable AnyKernel3 zip..."

    if [ ! -f "$BUILD_DIR/arch/arm64/boot/Image.gz-dtb" ]; then
        log_error "Kernel image not found at $BUILD_DIR/arch/arm64/boot/Image.gz-dtb"
        return 1
    fi

    if ! command -v zip &> /dev/null; then
        log_error "zip not found! Install with: sudo apt install zip"
        return 1
    fi

    local work_dir="AnyKernel3"
    local zip_name="SuzakuKernel-begonia-$(date +%Y%m%d-%H%M%S)-AnyKernel3.zip"

    rm -rf "$work_dir"
    git clone --depth 1 https://github.com/osm0sis/AnyKernel3.git "$work_dir"
    rm -rf "$work_dir/.git" "$work_dir/.github"

    cp "$BUILD_DIR/arch/arm64/boot/Image.gz-dtb" "$work_dir/Image.gz-dtb"

    # Replace template anykernel.sh with begonia config (based on requiredroot/powa_karnal)
    cat > "$work_dir/anykernel.sh" <<'AK3EOF'
# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
properties() { '
kernel.string=Suzaku Kernel V2 for Redmi Note 8 Pro (begonia)
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
# begonia (Redmi Note 8 Pro) is an A-only device: a single boot partition,
# no A/B slot suffix. Keep IS_SLOT_DEVICE=0 (AnyKernel3 default for A-only).
IS_SLOT_DEVICE=0;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh;

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
chmod -R 750 $RAMDISK/*;
chown -R root:root $RAMDISK/*;

## AnyKernel install
dump_boot;

write_boot;

## end install
AK3EOF

    mkdir -p build_output
    (cd "$work_dir" && zip -r9 "../build_output/$zip_name" . -x '.git/*' '.gitignore' '.github/*')
    rm -rf "$work_dir"

    log_info "AnyKernel3 zip created: build_output/$zip_name"

    # Never hand out an unverified zip: check payload + begonia anykernel.sh config
    if [ -f "$ROOT_DIR/verify_ak3_zip.sh" ]; then
        if bash "$ROOT_DIR/verify_ak3_zip.sh" "build_output/$zip_name"; then
            log_info "AnyKernel3 zip verified."
        else
            log_error "AnyKernel3 zip verification FAILED - do not flash this zip!"
            return 1
        fi
    fi
}

# Main function
main() {
    log_info "========================================="
    log_info "Begonia Kernel Build Script"
    log_info "With KernelSU and SUSFS Support"
    log_info "========================================="
    log_info ""
    
    check_dependencies
    prepare
    configure
    build
    package
    package_anykernel || log_warn "AnyKernel3 packaging failed (non-fatal)"
    
    log_info "========================================="
    log_info "Build completed successfully!"
    log_info "========================================="
}

# Run main function
main "$@"
