# A8 (issue #16): existing QEMU AIC models and feasibility of a test bench without a GIC

Research date: 2026-10-13. Method: read-only `gh api` (repository search, file tree, contents of `hw/intc/apple_aic.c`) and reading of the Linux driver `drivers/irqchip/irq-apple-aic.c` (torvalds/linux, saved only in the scratchpad). No clone, no download to disk in the repo.
Convention: [verified: source] or [hypothesis].

## 1. Projects found

| # | Project | Declared license | Stars | Last commit | QEMU base | Relevance |
|---|----------|--------------------|--------|---------------|-----------|-----------|
| 1 | https://github.com/TrungNguyen1909/qemu-t8030 (iPhone 11, A13) | GitHub: NOASSERTION (repo = QEMU fork, `COPYING` GPL-2.0, 17992 bytes) | 2204 | 2022-10-22, archived | 7.1.0 (`VERSION.QEMU`) | high: clean AIC model, 683 lines |
| 2 | https://github.com/ChefKissInc/Inferno (continuation of #1, "QEMUAppleSilicon") | `COPYING` GPL-2.0 (17880 bytes), but **the AIC file has an AGPL-3.0-or-later header** | 3955 | 2026-07-29 (repo pushed 2026-09-20) | 10.2.2 / 10.1.3 "hybrid" (tag `ChefKissInferno-1.0.0rc1_QEMU-10.2.2_10.1.3_hybrid`) | high for API updates, 636 lines |
| 3 | https://github.com/jprx/qemu-sptm (`-M darwin` machine, submodule of `darwin-vm`) | NOASSERTION (QEMU GPL-2.0) | 52 | 2026-09-26 | 11.1.0 (`VERSION`) | medium: "fake" AIC, 280 lines |
| 4 | https://github.com/alephsecurity/xnu-qemu-arm64 (iOS 12, s8000) | NOASSERTION | 1459 | 2021-09-16 | about 4.x [hypothesis] | low: not read, no AIC modeled as far as I know [hypothesis] |
| 5 | https://github.com/imbushuo/qemu (HVF patches for Apple Silicon hosts) | NOASSERTION | 44 | 2026-09-27 | n/a | none: does not emulate an Apple SoC, only uses the M1 host |

All table data: [verified: `gh api repos/...` and `.../git/trees`]. The line counts are those of the file `hw/intc/apple_aic.c` [verified: `wc -l` on the content].
Searched without results: GitHub queries "qemu apple aic", "qemu t8030 aic", "apple aic qemu hw/intc" (0 results); no Asahi fork for tests with emulated AIC found [verified: empty searches]; I did not search Corellium open (no known public sources, [hypothesis]).

## 2. What the three AIC models do

### 2.1 qemu-t8030 (`hw/intc/apple_aic.c`, 683 lines, 1033-byte header)
- Register layout = **AIC "v1" as in Linux** [verified: comparison with `irq-apple-aic.c`]: REV 0x0000, CAP0 0x0004, RST 0x000C, GLB_CFG 0x0010, WHOAMI 0x2000, IACK/EVENT 0x2004, IPI_SET 0x2008, IPI_CLR 0x200C, IPI_MASK_SET/CLR 0x2024/0x2028, DEFER_SET/CLR 0x202C/0x2030, EIR_DEST (target cpu) 0x3000, SW_SET 0x4000, SW_CLR 0x4080, MASK_SET 0x4100, MASK_CLR 0x4180, INT_RO 0x4200, per-CPU alias at 0x5000 + n*0x80. In Linux the AICv1 sw_set falls at 0x3000 + 4*0x400 = 0x4000 [verified: aic.c lines ~1000-1033, my calculation].
- The code says `AIC_VERSION 2` and `REV` returns 2, but the map is the Linux v1 one. It is not AICv2 (which has IRQ_CFG at 0x2000, mask at 0x6400 etc. in the jprx model) [verified: code].
- IRQ: per-line state (set_irq from gpio in), masking via MASK_SET/CLR, per-CPU destination via EIR_DEST, SW_SET/SW_CLR, INT_RO. An interrupt delivered with IACK is masked (hardware-like behavior, auto-mask). IACK event encoding: EXT = 0x10000 | num, IPI = 0x40001 (other) / 0x40002 (self), 0 = spurious. Matches Linux's `AIC_EVENT_TYPE_IRQ=1`, `TYPE_IPI=4`, `IPI_OTHER=1`, `IPI_SELF=2` [verified].
- Legacy IPI: SET/CLR/MASK/DEFER implemented [verified: code]. The "fast IPIs" (sysreg `IPI_RR_*`) are NOT in the AIC: they are in the CPU model `apple_a13.c`, which exposes a `fast_ipi` gpio towards FIQ [verified: `apple_a13.c` realize].
- ARM timer -> FIQ: **not in the AIC**. In the CPU model a 16-line `TYPE_OR_IRQ` is connected to `ARM_CPU_FIQ`; `GTIMER_VIRT` goes to line 0 and `fast_ipi` to line 1 [verified: `apple_a13.c`, ~lines 564-588]. This is the exact scheme we need for the timer.
- Costs/limits: CPU/IRQ count derived from the Apple device tree (`ipid-mask`, `#shared-timestamps`, node `aic-timebase`) via `hw/arm/xnu_dtb.h` (fork-specific APIs); a `QEMUTimer` thread every 64 us (`kAICWT`) that calls update (polling); `msi_nonbroken = true` (hack); `#define AIC_CPU_COUNT 6` and T8030 hand-coded; timebase offsets 0x20/0x28 (emulated 24 MHz counter) in the AIC register [verified: code].
- License: file without header; contributed by Trung Nguyen in a fork of QEMU 7.1 (GPL-2.0). Must be treated as GPL-2.0 [hypothesis, to be checked with the author if verbatim copying is wanted].

### 2.2 Inferno (`hw/intc/apple_aic.c`, 636 lines)
- Same model as #1 ported to QEMU 10.x: same register layout, same event codes, same structure. Changed: names (`REG_AIC_*`), `set_bit32`, DT API (`hw/arm/apple-silicon/dt.h`, `AppleDTNode`) [verified: header and code].
- **File license: AGPL-3.0-or-later** (copyright 2024-2026 VisualEhrmanntraut and Christian Inci) [verified: file header]. Incorporating it into a modified QEMU binary is problematic: AGPL §13 imposes the source offer even for network use, and the combination with GPL-2.0-only QEMU needs checking. Do not copy without an explicit decision.
- It is the best source to see how the QEMU 10.x API is adapted, but not for direct reuse.

### 2.3 qemu-sptm / darwin-vm (`hw/intc/apple_aic.c`, 280 lines)
- Declaredly "minimal": reads `compatible` "aic,N" and supports v1, v2, v3; knows the IRQ count, enables/disables, acks, and **routes everything to a single line towards core 0** [verified: header comment and code].
- For v1: IACK 0x2004, num ints 0x4, disable 0x4100, enable 0x4180, ack 0x4080 (note: their naming is inverted relative to Linux where 0x4100 is MASK_SET) [verified: comparison with aic.c; suggests they use "ENABLE=clear mask"; consistent].
- **No IPI, no per-CPU DEST, no SW_SET**, multicore does not work; the README declares FIQ/UART but the AIC does not handle the timer [verified: README and code].
- APIs updated to QEMU 11.1: `hw/core/sysbus.h`, `hw/core/irq.h` (includes moved in the new tree), `OBJECT_DECLARE_SIMPLE_TYPE`, `class_init(..., const void *data)` [verified: code]. Useful as a reference for current APIs.
- License not explicit in the file [verified: no header]; repo is a QEMU fork, GPL-2.0.

## 3. Grafting onto aarch64 `virt` or onto a "watari-virt" machine

What is needed in QEMU (stable APIs, independent of the forks) [hypothesis, based on the code read]:
1. New sysbus device `watari-aic` (modeled on #1/#3): `qdev_init_gpio_in(n_irq)`, one `sysbus_init_irq` per CPU, MMIO region.
2. New machine `hw/arm/watari-virt.c` (reduced copy of `virt.c` without `create_gic()`): CPU, RAM, PL011/UART, flash/UEFI, PCIe ecam, virtio-mmio. The device IRQ lines (`virt` uses GIC SPIs: UART, PCIe INTx, virtio) must be connected to `qdev_get_gpio_in(aic, n)`.
3. For each CPU: `sysbus_connect_irq(aic, cpu, qdev_get_gpio_in(cpu, ARM_CPU_IRQ))` as in `t8030.c` (~lines 768-772) [verified].
4. Timer: connect the CPU's outgoing `GTIMER_*` to `ARM_CPU_FIQ` (via `TYPE_OR_IRQ` as in `apple_a13.c`); on the standard `virt` they are instead GIC PPIs. This is the part that makes the bench a faithful imitation of the M1, where the timer arrives as FIQ and not via the AIC [verified for t8030; for M1: Linux creates the AIC_TMR_EL0_PHYS/VIRT FIQs inside the AIC as a separate domain, aic.c ~lines 552-600].
5. M1 fast IPIs (t8103 uses `aic1_local_fipi_info` in Linux, aic.c ~lines 308-312 [verified]): require IMP-DEF sysregs in the CPU (`IPI_RR_*`, `IPI_SR_EL1`, etc.) that QEMU TCG does not have. On `watari-virt` with CPU `max`/`cortex-a72` these do not exist; for our tests it is realistic to use only legacy IPIs (like the AIC with `compatible` "apple,aic") [verified: Linux supports `apple,aic` = no fast IPI].
6. The Apple device tree (ADT) is not needed: the info is supplied as device properties (`num-irq`, `num-cpu`) instead of reading `ipid-mask`, thus eliminating the `xnu_dtb.h` dependency.

Dependencies of #1 to remove: `hw/arm/xnu_dtb.h`, `msi_nonbroken`, the 64 us polling timer (replaceable with immediate computation in `set_irq`), the timebase in the register. APIs to update relative to current QEMU: `VMSTATE_*` arrays, `DEVICE_NATIVE_ENDIAN`, `hw/core/*` includes, `class_init(const void *)`, `qemu_mutex_unlock` inside `WITH_QEMU_LOCK_GUARD` (to be rewritten) [hypothesis].

Effort: porting #1 to a `watari-virt` on recent QEMU: 1-2 days for the device + 2-3 for the machine and the device wiring.

## 4. Recommendation

**Write a minimal AICv1 model from scratch (about 250-350 lines), using #1 (qemu-t8030, GPL-2.0, 683 lines) as a structure reference and the Linux driver as the register specification, and #3 (qemu-sptm) as a reference for current QEMU APIs.**
- Why not reuse a whole project: Inferno/t8030 carry an entire iPhone machine (ADT, SEP, DART, etc.) irrelevant to us; Inferno has AGPL on the AIC file; t8030 is on QEMU 7.1 and archived.
- Why not extract the file as is: it depends on the Apple DT, has hacks (`msi_nonbroken`, polling) and T8030 hardcodes; cleaning it costs as much as rewriting it, and the license of #1 is implicit.
- If in the future copying is preferred, use only #1 with a GPL-2.0 provenance header and ask the author for confirmation [hypothesis on license].

Estimate in days [hypothesis]:
| Activity | Days |
|---|---|
| AICv1 device (IRQ, mask, dest, SW_SET/CLR, IACK, legacy IPI), test with the Linux driver `irq-apple-aic` | 2 |
| `watari-virt` machine (starting from `virt.c` without GIC, UART, PCIe, virtio, IRQ wiring to AIC) | 3 |
| ARM timer -> FIQ (OR gate on the CPU) and test | 1 |
| Bring-up test with UEFI/Windows ARM64 + debug | 2-3 |
| Total | about 8-9 |

## 5. Minimum the model must do for our tests

Specified by the m1n1/Linux drivers (`irq-apple-aic.c`):
1. Info: read of `0x0004` with `NR_IRQ` in the low 16 bits (Linux `AIC_INFO_NR_IRQ`); `0x2000 WHOAMI` = CPU id.
2. Masks: `MASK_SET 0x4100 + 4*(n/32)`, `MASK_CLR 0x4180 + 4*(n/32)`, all masked after reset; reading `MASK_*` returns the state.
3. Target: `TARGET_CPU 0x3000 + 4*n` (Linux writes a CPU bitmask); default CPU0.
4. Software sources: `SW_SET 0x4000`, `SW_CLR 0x4080` (used for tests and for `irq_set_irqchip_state`).
5. `EVENT 0x2004`: returns `(1<<16)|n` for a pending, unmasked external IRQ destined for that CPU; 0 when there are none (Linux loops until `event != 0`); the delivered IRQ stays masked until `MASK_CLR` (eoi) [verified: behavior in Linux `aic_handle_irq` and `aic_irq_eoi`].
6. Legacy IPI: `IPI_SEND 0x2008` (bit per CPU and `BIT(31)` self), `IPI_ACK 0x200C`, `IPI_MASK_SET/CLR 0x2024/0x2028`; event `(4<<16)|1` (other) or `(4<<16)|2` (self) [verified: aic.c lines 82-96].
7. Output: one IRQ line per CPU towards `ARM_CPU_IRQ`, raised if at least one unmasked cause exists, lowered after the event is consumed.
8. Timer: no AIC register; the ARM timers (physical and virtual EL1/EL0) must be wired to `ARM_CPU_FIQ` in the machine, with a maskable FIQ as on M1. If we also need the "FIQ via AIC" as a separate domain (Linux `AIC_TMR_*`, `VM_TMR_FIQ_ENA_EL2`), Apple sysregs (`SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2`) are needed in the CPU: outside the minimum, to be evaluated with F0 FIQ (`re/f0-m1-fiq-irq.md`) [hypothesis].
9. Not needed for the minimum: DEFER IPI, GLB_CFG, timestamps, fast IPI, PMU FIQ, multi-die, AICv2/v3.

## 6. Unverified items / to do

- I did not read the m1n1 driver (`src/aic.c`) directly: the register specification is deduced from Linux and the t8030 model [hypothesis for m1n1].
- Actual license of the qemu-t8030 AIC file not declared in the file [hypothesis: GPL-2.0].
- To verify whether QEMU 10.x/11.x already has `hw/arm/virt` with a `gic-version=none` option or similar: from my research official QEMU does not allow removing the GIC [data from the user's brief, not re-verified].
