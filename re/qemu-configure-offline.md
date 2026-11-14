<!-- SPDX-License-Identifier: MIT -->
# QEMU 11.1.2 `configure` without any download (#42, task T9, 2026-11-13)

**Result: unblocked.** `configure` passes offline (exit 0, `build.ninja` generated, `ninja -n` = 3165 steps).
The build itself was NOT run. Nothing was downloaded or installed. [verified: commands below, run on the Mac]

## Inventory of the Pythons on the Mac [verified]
| Python | version | setuptools | wheel | pip | tomllib/tomli | usable by QEMU's mkvenv |
|---|---|---|---|---|---|---|
| `/usr/bin/python3` (= Command Line Tools) | 3.9.6 | 58.0.4 | 0.37.0 | 21.2.4 (+ a pip 26.0.1 in `~/Library/Python/3.9` user site) | no tomli, but pip vendors one | yes, with a shim (below) |
| Homebrew 3.12.13 / 3.13.13 / 3.14.7 | | none | 0.47.0 (3.12) | yes | tomllib | no: no setuptools; setuptools 58 would need `distutils`, removed in 3.12 |

QEMU needs Python >= 3.9 (`configure` line ~506). Meson 1.11.1, pycotap and qemu.qmp are vendored in
`python/wheels/`, so nothing else has to come from PyPI. `ensurepip` is not needed (the venv is created with
system site packages and pip comes from the CLT Python); it was only used implicitly by `venv` for Homebrew
attempts, inside the throw-away build dir.

## What failed, in order (each fix removed one blocker)
1. `--python=/usr/bin/python3`: "found no usable tomli" (Python 3.9 has no tomllib).
2. Homebrew 3.12 + setuptools copied from the CLT: `No module named 'distutils'` (setuptools 58 needs it).
3. CLT 3.9 + a tomli shim: the editable install of QEMU's `python/` failed, "missing the 'build_editable' hook":
   the venv picked up the pip 26.0.1 of `~/Library/Python/3.9` (user site), which dropped the legacy
   `setup.py develop` path. Fix: `PYTHONNOUSERSITE=1` (system pip 21.2.4 falls back to `setup.py develop`).
4. meson: `fdt disabled but required by targets aarch64-softmmu`: Homebrew's `dtc` (libfdt) is installed but not on the
   default search path (the `dtc` subproject would be a download). Fix: `--extra-cflags=-I/opt/homebrew/include
   --extra-ldflags=-L/opt/homebrew/lib`.

## The command (this is what `tools/qemu-watari/build.sh` now runs)
```
# once: tomli shim = re-export of the tomli vendored inside the CLT pip (a file in the scratch image only)
mkdir -p /private/tmp/watari-qemu/pyshim/tomli
printf 'from pip._vendor.tomli import *\nfrom pip._vendor.tomli import loads, load\n' > /private/tmp/watari-qemu/pyshim/tomli/__init__.py
cd /private/tmp/watari-qemu/build
PIP_NO_INDEX=1 PIP_DISABLE_PIP_VERSION_CHECK=1 PYTHONNOUSERSITE=1 PYTHONPATH=/private/tmp/watari-qemu/pyshim \
  ../src/qemu-11.1.2/configure --python=/usr/bin/python3 --target-list=aarch64-softmmu --disable-docs \
  --disable-tools --disable-guest-agent --disable-user --disable-werror --disable-download \
  --extra-cflags=-I/opt/homebrew/include --extra-ldflags=-L/opt/homebrew/lib
PYTHONNOUSERSITE=1 ninja        # not run yet: 3165 steps, expect tens of minutes to ~1 h on the M1 [hypothesis]
```
meson prints "Python 3.9 is EOL, meson 1.12 will require 3.10": harmless for the vendored 1.11.1.

## Safety notes
- Network: `PIP_NO_INDEX=1`, `--disable-download` (meson `wrap_mode: nodownload`); the configure log shows only
  vendored wheels (`--no-index --find-links python/wheels`) being installed into `build/pyvenv`.
- Created files: only inside the scratch image (`/private/tmp/watari-qemu/{build,pyshim}`, plus the venv `build/pyvenv`).
  System Python, Homebrew and the installed QEMU untouched. Removal: `tools/uninstall-watari.sh --yes` (detaches and
  the data folder holds the image).
- The first attempts left `build-offline/` dirs; they were deleted.
