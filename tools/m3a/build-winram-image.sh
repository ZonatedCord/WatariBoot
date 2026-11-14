#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build a raw disk image for the "Windows from RAM" boot (WinPE / Setup held in
# RAM by EDK2, no storage driver on the M1). Contents: the ISO's boot files
# (efi\, boot\, bootmgfw.efi, bootmgr.efi) and sources\boot.wim ONLY.
# macOS built-ins only (hdiutil, diskutil, newfs_msdos, cp, shasum).
# The ISO is mounted read-only and never analysed: files are copied and
# listed by name and size. Output goes under $WATARI_DATA, never in the repo.
set -euo pipefail

usage() {
    cat <<USAGE
Usage: $0 [--iso PATH] [--out PATH] [--gpt|--mbr|--superfloppy] [--force] [--verify-only]
  --iso PATH       Windows 11 ARM64 ISO (default: \$WATARI_DATA/iso/Windows11_Client_arm64_it-it_26300_9457.iso)
  --out PATH       output image (default: \$WATARI_DATA/winram/winram.img); never inside the repo
  --gpt            GPT + one EFI System Partition, FAT32 (default)
  --mbr            MBR + one FAT32 partition
  --superfloppy    FAT32 directly on the disk, no partition table
  --force          overwrite an existing output
  --verify-only    only verify an existing image (read-only attach, list, detach)
Writes <out>.sha256 and <out>.manifest.txt (relative paths + sizes only).
USAGE
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd -P)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../env.sh"

ISO="$WATARI_DATA/iso/Windows11_Client_arm64_it-it_26300_9457.iso"
OUT="$WATARI_DATA/winram/winram.img"
LAYOUT=gpt
FORCE=0
VERIFY_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --iso) ISO="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --gpt) LAYOUT=gpt; shift ;;
        --mbr) LAYOUT=mbr; shift ;;
        --superfloppy) LAYOUT=superfloppy; shift ;;
        --force) FORCE=1; shift ;;
        --verify-only) VERIFY_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# Refuse any output inside the repo (Microsoft binaries must never land there).
mkdir -p "$(dirname "$OUT")"
OUT_ABS="$(cd "$(dirname "$OUT")" && pwd -P)/$(basename "$OUT")"
case "$OUT_ABS/" in
    "$REPO_ROOT"/*) echo "ERROR: refusing output inside the repo ($REPO_ROOT)" >&2; exit 1 ;;
esac
GIT_TOP="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || true)"
case "$OUT_ABS/" in
    "$GIT_TOP"/*) [ -n "$GIT_TOP" ] && { echo "ERROR: refusing output inside the repo ($GIT_TOP)" >&2; exit 1; } ;;
esac

TMP="$(mktemp -d "${TMPDIR:-/tmp}/winram.XXXXXX")"
ISO_MNT="$TMP/iso"; IMG_MNT="$TMP/img"
mkdir -p "$ISO_MNT" "$IMG_MNT"
ISO_ATTACHED=0; IMG_DEV=""; WORK=""
cleanup() {
    set +e
    [ -n "$IMG_DEV" ] && hdiutil detach "$IMG_DEV" -force -quiet 2>/dev/null
    [ "$ISO_ATTACHED" = 1 ] && hdiutil detach "$ISO_MNT" -force -quiet 2>/dev/null
    [ -n "$WORK" ] && rm -f "$WORK"
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

# Attach our own image (raw) and set IMG_DEV; refuse unless the device is a
# disk image whose backing file is exactly our file.
attach_checked() {  # args: image, extra hdiutil args...
    local img="$1"; shift
    local out dev
    out="$(hdiutil attach -imagekey diskimage-class=CRawDiskImage "$@" "$img")"
    dev="$(printf '%s\n' "$out" | awk 'NR==1{print $1}')"
    IMG_DEV="$dev"
    ATTACH_OUT="$out"
    if ! hdiutil info | grep -F "image-path" | grep -qF "$img"; then
        echo "ERROR: could not match $dev to $img; aborting" >&2; exit 1
    fi
    if ! diskutil info "$dev" | grep -q "Disk Image"; then
        echo "ERROR: $dev is not a disk image; aborting" >&2; exit 1
    fi
}

verify_image() {
    local img="$1" mnt
    echo "== verify $img"
    [ $(( $(stat -f %z "$img") % 512 )) -eq 0 ] || { echo "FAIL: size not a multiple of 512" >&2; return 1; }
    # -nomount: macOS does not auto-mount ESP-typed partitions; mount by hand.
    attach_checked "$img" -readonly -nobrowse -nomount
    mnt="$TMP/verify"; mkdir -p "$mnt"
    local part
    part="$(diskutil list "$IMG_DEV" | awk -v d="${IMG_DEV#/dev/}" '$NF ~ "^"d"s[0-9]+$" {p=$NF} END{print p}')"
    if [ -z "$part" ]; then part="${IMG_DEV#/dev/}"; fi   # superfloppy: whole disk
    diskutil mount readOnly -mountPoint "$mnt" "/dev/$part" >/dev/null || { echo "FAIL: cannot mount /dev/$part" >&2; return 1; }
    echo "device: $IMG_DEV partition: /dev/$part"
    ls -l "$mnt"; echo "-- sources:"; ls -l "$mnt/sources"
    [ -f "$mnt/sources/boot.wim" ] || { echo "FAIL: boot.wim missing" >&2; return 1; }
    [ -f "$mnt/efi/boot/bootaa64.efi" ] || { echo "FAIL: efi/boot/bootaa64.efi missing" >&2; return 1; }
    [ "$(ls "$mnt/sources" | wc -l)" -eq 1 ] || { echo "FAIL: sources has more than boot.wim" >&2; return 1; }
    diskutil unmount "$mnt" >/dev/null
    hdiutil detach "$IMG_DEV" -quiet; IMG_DEV=""
    echo "== partition table"; (gpt -r show "$img" 2>&1 || fdisk "$img" 2>&1) | head -12
    echo "verify OK"
}

if [ "$VERIFY_ONLY" = 1 ]; then
    [ -f "$OUT_ABS" ] || { echo "ERROR: $OUT_ABS not found" >&2; exit 1; }
    verify_image "$OUT_ABS"; exit 0
fi

[ -f "$ISO" ] || { echo "ERROR: ISO not found: $ISO" >&2; exit 1; }
if [ -e "$OUT_ABS" ] && [ "$FORCE" != 1 ]; then
    echo "ERROR: $OUT_ABS exists (use --force to overwrite)" >&2; exit 1
fi

echo "== mounting ISO read-only"
# If something else already holds this ISO attached (another session), reuse
# its mount read-only and leave it attached; otherwise attach it ourselves.
EXISTING_MNT="$(hdiutil info | awk -v iso="$ISO" '
    /^=====/ { inimg = 0 }
    index($0, "image-path") && index($0, iso) { inimg = 1 }
    inimg && index($0, "/Volumes/") && /\/dev\/disk/ { print substr($0, index($0, "/Volumes/")); exit }')"
if [ -n "$EXISTING_MNT" ] && [ -d "$EXISTING_MNT" ]; then
    echo "ISO already attached at: $EXISTING_MNT (reusing, will not detach)"
    ISO_MNT="$EXISTING_MNT"
else
    hdiutil attach -readonly -nobrowse -mountpoint "$ISO_MNT" "$ISO" >/dev/null
    ISO_ATTACHED=1
fi
[ -f "$ISO_MNT/sources/boot.wim" ] || { echo "ERROR: sources/boot.wim not in ISO" >&2; exit 1; }
[ -f "$ISO_MNT/efi/boot/bootaa64.efi" ] || { echo "ERROR: efi/boot/bootaa64.efi not in ISO" >&2; exit 1; }

# What to copy: efi\, boot\, root bootmgr* / bootmgfw* files, sources\boot.wim.
COPY_DIRS=(efi boot)
COPY_FILES=()
for f in "$ISO_MNT"/bootmgr* "$ISO_MNT"/bootmgfw*; do
    if [ -f "$f" ]; then COPY_FILES+=("$(basename "$f")"); fi
done
COPY_FILES+=(sources/boot.wim)

# Size: payload + per-entry cluster slack + 5% + 16 MiB, rounded up to 1 MiB.
PAYLOAD=0; NENT=0
for d in "${COPY_DIRS[@]}"; do
    PAYLOAD=$(( PAYLOAD + $(find "$ISO_MNT/$d" -type f -exec stat -f %z {} + | awk '{s+=$1} END{print s+0}') ))
    NENT=$(( NENT + $(find "$ISO_MNT/$d" | wc -l) ))
done
for f in "${COPY_FILES[@]}"; do PAYLOAD=$(( PAYLOAD + $(stat -f %z "$ISO_MNT/$f") )); NENT=$((NENT+1)); done
MIB=$((1024*1024))
FS_BYTES=$(( PAYLOAD + NENT*32768 + PAYLOAD/20 + 16*MIB ))
if [ "$LAYOUT" != superfloppy ]; then FS_BYTES=$(( FS_BYTES + 4*MIB )); fi  # partition headers + alignment
SIZE=$(( (FS_BYTES + MIB - 1) / MIB * MIB ))
echo "payload: $PAYLOAD bytes in $NENT entries; image size: $SIZE bytes ($((SIZE/MIB)) MiB), layout: $LAYOUT"

AVAIL_KB="$(df -k "$(dirname "$OUT_ABS")" | awk 'NR==2{print $4}')"
[ $(( AVAIL_KB * 1024 )) -gt $(( SIZE * 2 )) ] || { echo "ERROR: not enough free space" >&2; exit 1; }

WORK="$OUT_ABS.partial"
rm -f "$WORK"
dd if=/dev/zero of="$WORK" bs=1048576 count=0 seek=$((SIZE/MIB)) 2>/dev/null

echo "== creating $LAYOUT image"
LABEL=WINRAM
attach_checked "$WORK" -nomount
echo "device: $IMG_DEV"
if [ "$LAYOUT" = superfloppy ]; then
    newfs_msdos -F 32 -S 512 -v "$LABEL" "${IMG_DEV/disk/rdisk}" >/dev/null
    diskutil mount -mountPoint "$IMG_MNT" "$IMG_DEV" >/dev/null
else
    SCHEME=GPT; if [ "$LAYOUT" = mbr ]; then SCHEME=MBR; fi
    diskutil partitionDisk "$IMG_DEV" "$SCHEME" FAT32 "$LABEL" 100% >/dev/null
    # data partition = the last sN of this device
    PART="$(diskutil list "$IMG_DEV" | awk -v d="${IMG_DEV#/dev/}" '$NF ~ "^"d"s[0-9]+$" {p=$NF} END{print p}')"
    [ -n "$PART" ] || { echo "ERROR: cannot find data partition" >&2; diskutil list "$IMG_DEV" >&2; exit 1; }
    diskutil unmount "/dev/$PART" >/dev/null 2>&1 || true
    diskutil mount -mountPoint "$IMG_MNT" "/dev/$PART" >/dev/null
fi
mount | grep -qF "$(cd "$IMG_MNT" && pwd -P)" || { echo "ERROR: image volume not mounted" >&2; exit 1; }

echo "== copying"
for d in "${COPY_DIRS[@]}"; do cp -RX "$ISO_MNT/$d" "$IMG_MNT/$d"; done
for f in "${COPY_FILES[@]}"; do
    mkdir -p "$IMG_MNT/$(dirname "$f")"
    cp -X "$ISO_MNT/$f" "$IMG_MNT/$f"
done
# Drop macOS housekeeping so the image holds only Windows files.
rm -rf "$IMG_MNT/.fseventsd" "$IMG_MNT/.Spotlight-V100" "$IMG_MNT/.Trashes" 2>/dev/null || true
find "$IMG_MNT" -name '._*' -delete 2>/dev/null || true
sync

echo "== manifest"
( cd "$IMG_MNT" && find . -type f ! -name '.DS_Store' | sed 's|^\./||' | LC_ALL=C sort | while IFS= read -r p; do
      printf '%s\t%s\n' "$(stat -f %z "$p")" "$p"
  done ) > "$TMP/manifest.body"
{
    echo "# winram image manifest: layout=$LAYOUT size=$SIZE bytes; size in bytes, relative path (names and sizes only)"
    awk -F'\t' '{printf "%12d  %s\n", $1, $2}' "$TMP/manifest.body"
} > "$OUT_ABS.manifest.txt"

diskutil unmount "$IMG_MNT" >/dev/null 2>&1 || diskutil unmount force "$IMG_MNT" >/dev/null
hdiutil detach "$IMG_DEV" -quiet; IMG_DEV=""
if [ "$ISO_ATTACHED" = 1 ]; then hdiutil detach "$ISO_MNT" -quiet; ISO_ATTACHED=0; fi

# diskutil makes a "Microsoft Basic Data" partition; retype it as an EFI System
# Partition by patching the GPT entry in both tables and fixing the CRCs
# (macOS gpt(8) needs root to modify, so do it on the detached file).
if [ "$LAYOUT" = gpt ]; then
    python3 -I - "$WORK" <<'PY'
import struct, sys, zlib
ESP = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")  # C12A7328-F81F-11D2-BA4B-00A0C93EC93B
path = sys.argv[1]
with open(path, "r+b") as f:
    def hdr(lba):
        f.seek(lba * 512); return bytearray(f.read(512))
    p = hdr(1)
    assert p[:8] == b"EFI PART"
    alt = struct.unpack_from("<Q", p, 32)[0]
    for lba in (1, alt):
        h = hdr(lba)
        assert h[:8] == b"EFI PART", "bad GPT header at %d" % lba
        hs, = struct.unpack_from("<I", h, 12)
        elba, n, esz = struct.unpack_from("<QII", h, 72)
        f.seek(elba * 512); ents = bytearray(f.read(n * esz))
        for i in range(n):
            if any(ents[i*esz:i*esz+16]):
                ents[i*esz:i*esz+16] = ESP
        f.seek(elba * 512); f.write(ents)
        struct.pack_into("<I", h, 88, zlib.crc32(bytes(ents)) & 0xffffffff)
        struct.pack_into("<I", h, 16, 0)
        struct.pack_into("<I", h, 16, zlib.crc32(bytes(h[:hs])) & 0xffffffff)
        f.seek(lba * 512); f.write(h)
PY
fi

mv -f "$WORK" "$OUT_ABS"; WORK=""
( cd "$(dirname "$OUT_ABS")" && shasum -a 256 "$(basename "$OUT_ABS")" > "$OUT_ABS.sha256" )
echo "== done: $OUT_ABS ($(stat -f %z "$OUT_ABS") bytes)"
cat "$OUT_ABS.sha256"
verify_image "$OUT_ABS"
