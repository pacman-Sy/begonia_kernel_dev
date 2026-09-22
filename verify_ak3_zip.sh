#!/bin/bash
# Verify a built AnyKernel3 zip for begonia (Redmi Note 8 Pro) before flashing.
set -u
ZIP="${1:?usage: verify_ak3_zip.sh <zip>}"
work=$(mktemp -d)
unzip -q "$ZIP" -d "$work" || exit 1
fail=0
ok()   { echo "  PASS: $1"; }
bad()  { echo "  FAIL: $1"; fail=1; }

echo "== Files in zip =="
(cd "$work" && find . -maxdepth 1 -type f | sed 's|^\./||' | sort | tr '\n' ' '); echo

echo "== Kernel payload =="
[ -f "$work/Image.gz-dtb" ] && ok "Image.gz-dtb present" || bad "Image.gz-dtb missing"
for stale in Image Image.gz Image-dtb zImage Image.lz4 Image.fit; do
  [ -f "$work/$stale" ] && bad "stale payload $stale present (AK3 detection order would prefer it)"
done

# AK3 kernel auto-detection order (tools/ak3-core.sh split_boot)
detected=""
for i in zImage zImage-dtb Image Image-dtb Image.gz Image.gz-dtb Image.bz2 Image.bz2-dtb \
         Image.lzo Image.lzo-dtb Image.lzma Image.lzma-dtb Image.xz Image.xz-dtb Image.lz4 Image.lz4-dtb Image.fit; do
  if [ -f "$work/$i" ]; then detected=$i; break; fi
done
[ "$detected" = "Image.gz-dtb" ] && ok "AK3 would detect: $detected" || bad "AK3 would detect: $detected"

# payload structure: gzip(kernel) + appended dtb (MTK Image.gz-dtb convention)
if [ -f "$work/Image.gz-dtb" ]; then
python3 - "$work/Image.gz-dtb" <<'PY'
import sys, zlib
p = sys.argv[1]
data = open(p, 'rb').read()
assert data[:2] == b'\x1f\x8b', 'not a gzip stream'
d = zlib.decompressobj(31)          # 31 = gzip wrapper
kernel = d.decompress(data)
rest = d.unused_data
print(f"  PASS: gzip kernel inflates to {len(kernel)} bytes, appended {len(rest)} bytes, magic {rest[:4].hex()}")
sys.exit(0 if rest[:4] == b'\xd0\x0d\xfe\xed' else 1)   # FDT_MAGIC d00dfeed
PY
[ $? -eq 0 ] || fail=1
else
  bad "skipping payload structure check (no Image.gz-dtb)"
fi

echo "== anykernel.sh =="
[ -f "$work/anykernel.sh" ] || { bad "anykernel.sh missing"; exit 1; }
grep -q '^BLOCK=/dev/block/by-name/boot;$'            "$work/anykernel.sh" && ok "BLOCK=/dev/block/by-name/boot"            || bad "BLOCK path wrong"
grep -q '^IS_SLOT_DEVICE=0;$'                          "$work/anykernel.sh" && ok "IS_SLOT_DEVICE=0 (A-only)"                || bad "IS_SLOT_DEVICE wrong"
grep -q '^device.name1=begonia$'                       "$work/anykernel.sh" && ok "device.name1=begonia"                      || bad "device.name1 wrong"
grep -q '^device.name2=begonia_in$'                    "$work/anykernel.sh" && ok "device.name2=begonia_in"                   || bad "device.name2 wrong"
grep -q '^device.name3=begoniain$'                     "$work/anykernel.sh" && ok "device.name3=begoniain"                    || bad "device.name3 wrong"
grep -q '^do.devicecheck=1$'                           "$work/anykernel.sh" && ok "do.devicecheck=1"                          || bad "devicecheck off"
grep -q '^\. tools/ak3-core.sh;$'                      "$work/anykernel.sh" && ok "ak3-core.sh sourced"                       || bad "ak3-core.sh not sourced"
grep -q '^dump_boot;'                                  "$work/anykernel.sh" && ok "dump_boot called"                          || bad "dump_boot missing"
grep -q '^write_boot;'                                 "$work/anykernel.sh" && ok "write_boot called"                         || bad "write_boot missing"
if grep -qiE 'maguro|toroplus|toro$|tuna|omap_hsmmc|fstab\.tuna|init\.tuna' "$work/anykernel.sh"; then
  bad "leftover Galaxy Nexus template code"
else
  ok "no Galaxy Nexus template leftovers"
fi
grep -q '^kernel.string=' "$work/anykernel.sh" && ok "kernel.string present" || bad "kernel.string missing"

echo
[ $fail -eq 0 ] && echo "RESULT: zip is OK to flash" || echo "RESULT: PROBLEMS FOUND"
rm -rf "$work"
exit $fail
