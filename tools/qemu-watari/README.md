<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# tools/qemu-watari: QEMU with an Apple AIC next to the GIC (issue #42)

A patch series on top of **QEMU 11.1.2** (the version of the Homebrew QEMU used everywhere else in the
project) plus the script that builds it. The result is a test bench for Watari EL2 against Apple-like
interrupt hardware (an AICv1 register block, devices that interrupt through it) without a reboot of the
Mac. The Homebrew QEMU is never touched: our binary is used by explicit path (`QEMU=...`).

## License

QEMU is GPL-2.0. The files in this directory (`patches/*.patch`, `build.sh`, this README) and every new
QEMU source file they add are **GPL-2.0-or-later** (`SPDX-License-Identifier: GPL-2.0-or-later`), a separate
work from the MIT-licensed Watari EL2 layer in `el2/`, which includes no GPL code. The AIC model was written
from public documentation only (Asahi Linux `hw/soc/aic.md`, the Linux DT binding `apple,aic`, the register
table in `el2/include/wel2_aic.h`); nothing was copied from Inferno (AGPL), qemu-t8030 or the Linux driver.
QEMU's own device models were read for the API (`pl031.c`, `or-irq.h`, `virt.c`).

## What the series does

| Patch | Content |
|---|---|
| `0001-hw-intc-add-watari-aic.patch` | New device `watari-aic` (`hw/intc/watari_aic.c`, `include/hw/intc/watari_aic.h`): AICv1 layout of the t8103 (INFO, WHOAMI as the per-CPU view, EVENT with auto-mask, IPI_SEND/ACK/MASK_SET/MASK_CLR, TARGET_CPU, SW_SET/SW_CLR, MASK_SET/MASK_CLR, HW_STATE); one IRQ output per core. Simplifications are listed at the top of the file. |
| `0002-hw-arm-virt-add-aic-machine-option.patch` | `virt` machine options `aic=on` and `aic-routes=none\|rtc\|uart\|rtc+uart` (default `rtc+uart`). The GICv3 stays (QEMU provides the `ICH_*` CPU interface only with it, and the M1 has `ICH_*` too). The AIC is mapped at `0x0f004000` (0x8000 bytes, a free hole of the memory map), each CPU's IRQ line becomes the OR (`TYPE_OR_IRQ`) of the GIC and AIC outputs, and the selected devices (PL031 RTC, PL011 UART0) are wired to AIC inputs instead of GIC SPIs. The AIC input number of a device is its usual SPI number (RTC 2, UART0 1), so a layer that exposes AIC hwirq `h` as vINTID `32 + h` (the intr_map rule) keeps the INTIDs that the FDT / ACPI already announce. The FDT gets a node `watari-aic@f004000`: `compatible = "watari,aic-v1"`, `reg`, `watari,num-irq`, `watari,num-cpu`, `watari,routed-irqs`. |

## Build

Prerequisites (see `docs/INSTALLED.md`): Homebrew `ninja` (QEMU bundles meson), the data disk mounted, the
tarball in `$WATARI_DATA/qemu-watari/dl/` (downloaded by hand after the Time Machine backup; sha256 checked by
the script):

```
curl -fLO https://download.qemu.org/qemu-11.1.2.tar.xz      # 135 MB, into $WATARI_DATA/qemu-watari/dl/
tools/qemu-watari/build.sh             # unpack, patch, configure --target-list=aarch64-softmmu, ninja
tools/qemu-watari/build.sh --clean     # start from the tarball again (after changing a patch)
```

The script prints the binary path on stdout: `/private/tmp/watari-qemu/build/qemu-system-aarch64`.
QEMU's `configure` refuses a path with spaces and the data disk is called "Arthur Fonzarelli", so the work
tree lives in a 12 GB sparse APFS image (`$WATARI_DATA/qemu-watari/work.sparseimage`) that the script attaches
at `/private/tmp/watari-qemu` (and `tools/uninstall-watari.sh` detaches). After a reboot just run `build.sh`
again: it re-attaches the image and ninja finds the build tree intact.

## Use

```
export QEMU=/private/tmp/watari-qemu/build/qemu-system-aarch64
el2/boot/run-m1-6.sh                   # PL031 alarm through the AIC, 4 cores, re-target on CPU_OFF
CPUIF=trap el2/boot/run-m1-6.sh        # same, virtual CPU interface in TRAP_EMULATE mode
```

Any other `el2/boot/run-*.sh` honours `QEMU=` (stock `virt` behaviour, useful to check that our build boots
the existing tests). `run-m2.sh` also takes `MACHINE_OPTS=aic=on` (Windows on the hybrid machine).
`el2/boot/run-all.sh` runs `run-m1-6.sh` as an optional entry only when the binary exists.

## Regenerating a patch

`build.sh` keeps a pristine tree `src/orig` next to the patched `src/qemu-11.1.2`. After editing the patched
tree: `cd /private/tmp/watari-qemu/src && diff -urN orig/<dir> qemu-11.1.2/<dir> > ...` per patch, then
`build.sh --clean` to check that the series still applies from the tarball.

## Status (2026-11-03, stopped on request): what works and what is NOT done

- **Done:** the tarball is downloaded and sha256-checked; `ninja` is installed; the work image is created; the
  two patches are written and **apply cleanly** to the pristine 11.1.2 tree (checked with `patch`); the layer
  side (FDT node lookup, hybrid GIC+AIC `pirq` backend, AIC drain in the IRQ path, stage-2 hole over the AIC
  window, `run-m1-6.sh`, `QEMU=` / `MACHINE_OPTS=` knobs) builds and does nothing unless the FDT has the AIC node,
  so the default configuration is unchanged (`run-all.sh` with the Homebrew QEMU).
- **NOT done / NOT verified:** QEMU has **never been built**, with or without the patches, so the device model and
  the `virt` changes have not even been compiled, and `run-m1-6.sh` has never run (it exits 77 = SKIP without the
  binary). Phase 1 (stock build boots the existing tests) is therefore also open. Phase 4 (timer to FIQ,
  Windows on the hybrid machine) not started.
- **Blocker found, then solved offline (T9, 2026-11-13):** `configure` failed in the venv step; it now passes with no
  download using the Command Line Tools Python 3.9 plus a tomli shim, see `re/qemu-configure-offline.md` and F-28.
  `build.sh` carries the exact command. The build itself (3165 ninja steps) has still not been run.
- Expect compile errors on the first build of the patches (written against the 11.1.2 headers without compiling).
