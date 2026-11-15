<!-- SPDX-License-Identifier: MIT -->
# QEMU with the AIC as a bench (#42, task T9b, 2026-11-15)

**Result:** the patched QEMU 11.1.2 (patches 0001-0003 in `tools/qemu-watari/patches/`) builds offline, boots the layer on a
GICv3 + Watari AIC machine, and the day-2 test guest G1-G12 passes there. The layer runs its QEMU platform with the AIC
backend. `plat_m1` (Apple FIQ timer, fast IPI, Apple sysregs) is still NOT exercised by any bench. [verified: runs below]

## Build [verified]
- The work image was unmounted (reboot); `hdiutil attach` brought back the tree, configure from T9 was intact (no redo).
- The patches had never been applied to the tree; applied 0001, 0002; `ninja -j3` (3167 steps, ~25 min on the 8 GB host, no
  download). One compile error in our code: `watari_aic.c` used `error_setg` without `#include "qapi/error.h"` (fixed in 0001).
  Nothing else failed. The series (0001-0003) re-applies cleanly on the pristine tree and gives the tree that was built.
- Patch 0003 (new): `virt` option `ram-base=<multiple of 1 GiB>` moves guest RAM (memmap VIRT_MEM, FDT /memory, loader start,
  SMBIOS). With it set, the "high IO region at >= RAM base + 255 GiB" bump is skipped, so the high regions follow the RAM.
- Nothing installed or downloaded; `make install` never run; everything lives in `/private/tmp/watari-qemu` (already in INSTALLED.md).

## What the bench runs [verified]
| Run | Result |
|---|---|
| `QEMU=... el2/boot/run-m1-6.sh` (PL031 alarm -> AIC input 2 -> vINTID 34, 4 cores, re-target on CPU_OFF) | PASS (first time ever; no layer change needed) |
| `el2/boot/run-aic.sh` (test guest G1-G12 on `aic=on`) | PASS, G8/G12 SKIP as on the other benches; layer logs "AIC at 0xf004000" |
| `RAM_BASE=0x800000000 el2/boot/run-aic.sh` | PASS, G1-G12 as above; layer log: "RAM from the FDT /memory node, base 0x800000000", RAM end 0x820000000 |

## What is covered and what is not
Covered: layer AIC discovery from the FDT, AIC EVENT drain, hwirq -> vINTID injection, TARGET_CPU re-target, stage-2 hole over the
AIC window, hybrid GIC+AIC pirq backend.

Not covered (QEMU has no model, plat_m1 is selected only when the FDT says `is_m1`): the Apple implementation-defined sysregs
the layer touches in `apple_sysreg.c`: `VM_TMR_FIQ_ENA_EL2` (S3_5_C15_C1_3), `CNTV/CNTP_CTL_EL02` (S3_5_C14_C3_1 / C2_1),
`CNTHV/CNTHP_CTL` (S3_4_C14_C3_1 / C2_1), fast IPI `IPI_SR` (S3_5_C15_C1_1) and `IPI_RR_GLOBAL` (S3_5_C15_C0_1), `PMCR0`
(S3_1_C15_C0_0), `UPMCR0`, `UPMSR`, `ICH_HCR` / `ICH_MISR` Apple encodings (S3_4_C12_C11_0/2), the `HCR_EL2.E2H` stuck at 1, the
FIQ delivery of the timer and IPIs (QEMU delivers them as GIC PPIs), and `idreg.c` ID-register values.
Estimate to cover them [hypothesis]: a QEMU `-cpu` variant with these sysregs registered as `ARMCPRegInfo` (write-through
stubs: 1-2 days), plus routing the timer outputs and a fast-IPI model to the CPU FIQ line, and an `is_m1`-style FDT for the
bench so `plat_select` picks plat_m1 (2-3 days more; the FIQ vs IRQ semantics are where bugs would hide). No plat_select hook
was added: the default and the real-M1 behaviour are unchanged.

## RAM above 4 GiB (coordinator's extra goal)
**Possible, done, verified.** `ram-base=0x800000000` puts all RAM (512 MB in the test) at the M1's t8103 base with nothing below
4 GiB. The layer reads it from the FDT /memory node and the guest is entered from `base + 0x280000` (`run-aic.sh` computes the
loader address). Peripherals (UART, GIC, AIC, RTC) stay in the low MMIO map, as the M1's do not collide either. EDK2 and the WinPE
path above 4 GiB are NOT run yet: they need the carve-out address and EDK2 platform RAM base to follow `RAM_BASE`
(`run-m1-*.sh` / `run-winram*.sh` hard-code 0x40000000-based loader addresses); the machine side is ready, add the
`MACHINE_OPTS=aic=on,ram-base=...` knob and the addresses to those scripts. Caveat: the high IO region (PCIe high ECAM / MMIO)
moves with the RAM end (no 255 GiB bump), so ECAM is no longer at 0x4010000000 on this machine.
