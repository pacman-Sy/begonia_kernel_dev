# Pox Kernel for Redmi Note 8 Pro (begonia)

[![Release](https://img.shields.io/badge/Version-0.9-blue.svg)](https://github.com)
[![Kernel](https://img.shields.io/badge/Kernel-Pox-purple.svg)](https://github.com)
[![Device](https://img.shields.io/badge/Device-begonia%20(MT6785)-green.svg)](https://github.com)
[![Maintainer](https://img.shields.io/badge/Maintainer-TXO%20R-orange.svg)](mailto:ravtx12best@gmail.com)

**Pox Kernel** is an advanced, rock-solid custom Linux kernel engineered for the **Redmi Note 8 Pro** (`begonia` / `begonia_in`, MediaTek Helio G90T / MT6785).

> *"We aim for stability, not for anything else."*

---

## Rock Editions & Branches (v0.9)

Pox Kernel releases are organized into specialized **Rock Editions**, each built for specific performance profiles:

| Edition | Branch | Focus & Highlights |
| :--- | :--- | :--- |
| **Granite** | `main` / `granite` | **Rock-Solid Stability Foundation**<br>• Hardware UVLO battery collapse protection (`2sec_reboot` fix)<br>• SCP SensorHub ringbuffer watchdog crash fix<br>• Chinese variant low-battery voice call reboot fix<br>• KernelPatch & APatch root support (`CONFIG_KALLSYMS_ALL`) |
| **Obsidian** | `memory-enhanced` / `obsidian` | **Frictionless Compressed Memory Engine**<br>• iOS-style on-demand asynchronous RAM compaction<br>• Watermark scale factor enlarged to 150 for smooth frame pacing<br>• `page-cluster=0` for 0ns ZRAM decompression latency<br>• Balanced swappiness (100) preventing background app killing |
| **Onyx** | `gaming` / `onyx` | **Peak Performance & Zero Frame-Drop Gaming**<br>• Unified Gaming Mode Controller (`/proc/perfmgr/gaming_mode`)<br>• Dynamic ROM performance profile detection (LineageOS, PixelOS, MIUI)<br>• FPSGO Ultra-Rescue with touch boost & Mali-G76 MC4 DVFS margin<br>• Schedutil instant 500us frequency ramping + COBRA big-core priority |

---

## Portable Build System

Pox Kernel includes a completely self-contained build and packaging environment. All dependencies (Android Clang 11.0.1, GCC 4.9 binutils, AnyKernel3) are managed in `./kerdevdep`.

### Quick Commands

```bash
# Build kernel and package AnyKernel3 flashable zip
./build.sh

# Fast package only (uses existing Image.gz-dtb)
./build.sh zip

# Menuconfig
./build.sh menuconfig

# Clean outputs
./build.sh clean
```

### Dynamic Customization

The build script dynamically adapts names, versions, and rock editions without modifying core code. You can customize them via variables in `build.sh` or through environment variables:

```bash
# Custom edition or version
KERNEL_NAME="Pox" KERNEL_VERSION="0.9" BRANCH_CODENAME="Onyx" ./build.sh

# Change maintainer or build branding
KERNEL_NAME="Pox" MAINTAINER="TXO R" ./build.sh zip
```

The build system automatically synchronizes:
- Output zip package naming (`build/Pox-<Edition>-<Version>-begonia-<hash>.zip`)
- Kernel local version string (`uname -r` reports `4.14.357-Pox-<Edition>-<Version>`)
- TWRP / OrangeFox Recovery installer banners
- AnyKernel3 `kernel.string` properties
- Installer `CHANGELOG.txt`

---

## Credits & Acknowledgments

- **Lead Developer & Maintainer**: TXO R (Pox Project) - `ravtx12best@gmail.com`
- **Linux Kernel Contributors**: Linus Torvalds and the worldwide Linux kernel developer community.
- **MediaTek Inc.**: MT6785 / Helio G90T board support packages and performance subsystem drivers.
- **osm0sis**: AnyKernel3 flashable zip packaging template and scripts.
- **The Begonia Community**: Developers, testers, and enthusiasts keeping the Redmi Note 8 Pro alive and fast.

---

## License

This project is licensed under the terms of the GNU General Public License version 2 (GPL-2.0). See [COPYING](COPYING) for complete details.
