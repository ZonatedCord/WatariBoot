#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds QEMU 11.1.2 (aarch64-softmmu only) with the Watari patch series (issue #42).
#   tools/qemu-watari/build.sh [--clean]   unpack, patch, configure, build
#
# Layout (all on the data disk, the Homebrew QEMU is never touched):
#   $WATARI_DATA/qemu-watari/dl/qemu-11.1.2.tar.xz   the tarball (downloaded by hand, see README.md)
#   $WATARI_DATA/qemu-watari/work.sparseimage        12 GB sparse APFS image, mounted at /private/tmp/watari-qemu
#   /private/tmp/watari-qemu/src/{orig,qemu-11.1.2}  pristine and patched source trees
#   /private/tmp/watari-qemu/build                   build tree
# Why an image: QEMU's configure/meson refuse a path with spaces, and the data disk is called
# "Arthur Fonzarelli" (a symlink does not help, the venv python resolves it). The image is attached
# by this script when needed and detached by tools/uninstall-watari.sh.
#
# Output: the path of the binary on stdout (progress on stderr).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/../env.sh"
VER=11.1.2
Q="$WATARI_DATA/qemu-watari"
IMG="$Q/work.sparseimage"
W=/private/tmp/watari-qemu
TARBALL="$Q/dl/qemu-$VER.tar.xz"
SHA256=731b5681e4bb18be313231579b8efd0296c5b015fa36dc533874b639ba838016
SRC="$W/src/qemu-$VER"
BLD="$W/build"

[ -f "$TARBALL" ] || { echo "ERROR: $TARBALL missing (download it first, see README.md)" >&2; exit 1; }
echo "$SHA256  $TARBALL" | shasum -a 256 -c - >&2

if [[ "$(mount)" != *" on $W "* ]]; then
    [ -f "$IMG" ] || hdiutil create -size 12g -type SPARSE -fs APFS -volname WatariQemu "$IMG" >&2
    mkdir -p "$W"
    hdiutil attach -nobrowse -mountpoint "$W" "$IMG" >&2
fi
if [ "${1:-}" = "--clean" ]; then rm -rf "$W/src" "$BLD"; fi

mkdir -p "$W/src" "$BLD"
if [ ! -d "$SRC" ]; then
    # A pristine copy ("orig") is kept next to the patched tree, for regenerating patches with diff -u.
    tar -xf "$TARBALL" -C "$W/src"
    mv "$SRC" "$W/src/orig"
    cp -R "$W/src/orig" "$SRC"
    for p in "$HERE"/patches/*.patch; do
        [ -f "$p" ] || continue
        echo "applying $(basename "$p")" >&2
        patch -d "$SRC" -p1 --no-backup-if-mismatch < "$p" >&2
    done
fi

cd "$BLD"
if [ ! -f build.ninja ]; then
    # Offline configure (F-28, re/qemu-configure-offline.md): the Command Line Tools Python 3.9 is the only
    # Python on the Mac that ships setuptools+wheel; it lacks tomli (shim re-exporting the copy vendored in its
    # pip) and a user-site pip 26 would refuse the editable install (PYTHONNOUSERSITE). Nothing is installed.
    SHIM="$W/pyshim"
    mkdir -p "$SHIM/tomli"
    printf 'from pip._vendor.tomli import *  # noqa\nfrom pip._vendor.tomli import loads, load  # noqa\n' > "$SHIM/tomli/__init__.py"
    PIP_NO_INDEX=1 PIP_DISABLE_PIP_VERSION_CHECK=1 PYTHONNOUSERSITE=1 PYTHONPATH="$SHIM" \
    "$SRC/configure" --python=/usr/bin/python3 --target-list=aarch64-softmmu --disable-docs --disable-tools \
        --disable-guest-agent --disable-user --disable-werror --disable-download \
        --extra-cflags=-I/opt/homebrew/include --extra-ldflags=-L/opt/homebrew/lib >&2
fi
PYTHONNOUSERSITE=1 ninja >&2
echo "$BLD/qemu-system-aarch64"
