# Draft ADR: First boot of Watari EL2 on the real M1 (hand-over, device tree, timer, PSCI)

- Status: **draft** (proposed 2026-11-10 by Ralph task T4, to be numbered at merge; needs Marco's approval)
- Design: `docs/design/watari-el2-m1boot/` (README, DESIGN, PLAN, RISKS)
- Related: `docs/adr/draft-plan-b-el2-layer.md`, `docs/adr/draft-timer-fiq-strategy.md`, `docs/adr/draft-m1-device-interrupts.md`, `docs/adr/draft-el2-license.md`, `re/m3a-ehw0-measurements.md`
- Marking and short source names as in DESIGN section 0 (`M:` = m1n1 v1.6.1, `L:` = Linux v6.12, `D:` = Asahi docs, `HW` = day-1 measurements, `R:` = this repository).

## Context

Watari EL2 boots Windows in QEMU (M2). Day 1 on the real M1 (2026-11-10) measured the hardware facts and showed that a list-register interrupt reaches EL1. The next step is the first run of the layer itself on the M1, started from m1n1 v1.6.1, with a small EL1 test guest. The M1 differs from the bench in five places: no firmware PSCI and no EL3; no physical GIC (the AIC for IRQ, system-register sources for FIQ); a FIQ-only timer; RAM at `0x800000000` with a load address chosen at run time; and a Samsung-style UART that is not reachable over the day-1 USB link. [verified: HW; DESIGN 1-2]

## Decisions

1. **Hand-over format: m1n1's Linux boot path, driven from the proxy, nothing persistent.** The host loads the layer, the guest and a DTB into RAM and calls `kboot_prepare_dt` + `kboot_boot`. The layer is entered at EL2 with the MMU and caches off, A/I/F masked, x0 = the FDT m1n1 prepared [verified: M:src/kboot.c:2862-2885, M:src/main.c:190-212, M:src/memory.c:636-643, M:src/exception.c:180-183]. The layer keeps its arm64 `Image` header, so the same binary can later be appended to m1n1 for a standalone boot [verified: M:src/payload.c:22-31, 134-155]. `chainload.py` is not used: it replaces m1n1 and passes Apple boot arguments, not an FDT [verified: M:proxyclient/tools/chainload.py:24-36, 160-170].
2. **The layer becomes position independent** (PIE, self-applied `R_AARCH64_RELATIVE`) and lives in one 32 MB, 32 MB aligned block picked by the loader. One binary runs in QEMU and on the M1; the platform is chosen at run time from the FDT (`apple,aic` present), behind a `plat_ops` seam. [DESIGN 2.3, 4.3]
3. **Device tree source: an FDT prepared by m1n1 from our own minimal j293 DTS** (cpus with `spin-table`, memory, AIC, UART0, watchdog, timer, an empty framebuffer node), compiled with the `dtc` already installed. m1n1 fills memory, the cores' release addresses and the framebuffer from the ADT [verified: M:src/kboot.c:180-240, 350-452, 541-610]. The upstream `t8103-j293.dtb` (`GPL-2.0+ OR MIT` [verified: L:arch/arm64/boot/dts/apple/t8103.dtsi:1]) is the manual fallback if m1n1 rejects the minimal one; that failure happens before any of our code runs. The layer does not parse the ADT. Acceptance of the minimal DTB by m1n1 is **[hypothesis]** until the dry run.
4. **Timer and FIQ path.** `HCR_EL2.FMO = IMO = 1`. The guest programs `CNTV`/`CNTP` directly; their FIQ is taken at EL2, masked at the source with the `VM_TMR_FIQ_ENA_EL2` gate bits (the guest's control register stays untouched) and injected as vPPI 27 / 30 through the existing vGIC; the gate reopens when the guest has silenced the timer [verified registers: L:drivers/irqchip/irq-apple-aic.c:183-185, 485-505, 566-576]. The same handler serves the fast IPI (the layer's cross-core kick) and the unmaskable vGIC maintenance interrupt, and switches the PMU sources off [verified: L:drivers/irqchip/irq-apple-aic.c:167-180, 411-420, 580-597, 826-853].
5. **Virtual CPU interface: hardware list registers with all `ICC_*` traps (`TRAP_EMULATE`), forced on the M1.** The M1 implements `ICH_*` although `ID_AA64PFR0_EL1.GIC` is 0 [verified: HW rows 3 and 5, and the list-register test]; its SEIS is broken, so the untrapped mode is not safe; KVM uses the same traps on these cores [verified: L:arch/arm64/kvm/vgic/vgic-v3.c:616-636, 705-715]. The guest's view of `ID_AA64PFR0_EL1.GIC` is set to 1 through `HCR_EL2.TID3`, as KVM does [verified: L:arch/arm64/kvm/sys_regs.c:1720-1723]. This is a hard dependency: without the hardware virtual CPU interface an unmodified GICv3 guest cannot be served, because an UNDEFINED `ICC_*` access at EL1 is not trappable to EL2 **[hypothesis: Arm ARM routing rule, section not re-read]**.
6. **PSCI is implemented entirely in the layer; the layer never executes `smc` on the M1** (no EL3 [verified: HW row 5]). `CPU_ON` releases a core from m1n1's spin table: store the layer's secondary entry at the core's `cpu-release-addr`, clean the cache line, `SEV` [verified: M:src/smp.c:60-106, 490-530; L:arch/arm64/kernel/smp_spin_table.c:85-100]. `CPU_OFF` parks the core in a loop inside the layer (no power-down). `SYSTEM_RESET` uses the watchdog sequence m1n1 uses for its own reboot [verified: M:src/wdt.c:50-61]. `SYSTEM_OFF` halts with a message until a register-level power-off is known.
7. **Output: the built-in display first** (checkpoint squares and a small text console on the framebuffer m1n1 describes in the FDT), **UART0 polled transmit second.** m1n1's USB link ends at hand-over [verified: M:src/main.c:196-204]; the UART is reachable only over the DFU port's debug pins with `macvdmtool`, a SuperSpeed cable and `sudo` on an Apple Silicon host [verified: D:hw/soc/serial-debug.md]. Whether that kit is used is a decision for Marco and Ciani.
8. **First guest and hardware method.** A separate EL1 test binary with its own exception vectors (the same binary runs under the layer in QEMU), eleven checks from "hello" to `SYSTEM_RESET`. On hardware the runs climb a ladder (dry run of the DTB; layer alone; guest on one core; timers; all cores), each ending in a watchdog reset.

## Status of the claims

Verified on 2026-11-10 from sources or by day-1 measurement: m1n1's hand-over state and SMP mechanism, the FIQ sources and registers, the UART and watchdog registers, the presence of the virtual CPU interface and KVM's use of its traps on M1 cores. Hypotheses until hardware: that every `ICC_*` trap behaves on this machine as on the bench (RISKS R-1, Q-1); what the screen shows after m1n1's framebuffer shutdown (R-2, Q-5); acceptance of the minimal DTB (R-3); cache maintenance at the MMU boundaries (R-7).

## Consequences

- New code, all host-testable behind accessors: UART driver, framebuffer output, FDT queries, Apple FIQ module, spin-table/park core start, ID-register emulation, the platform seam. One invasive change: the position-independent build and the run-time memory layout.
- `el2/boot/gic_phys.c` and the firmware SMC calls become the QEMU back end; none of it runs on the M1.
- Day 2 changes nothing persistent on the Mac: RAM, CPU registers, AIC masks, UART, framebuffer, and the watchdog only for the reset. A hang is ended with the power button; macOS stays the default boot.
- The serial console is optional and has a footprint on the host Mac (`sudo`, a build).
- Real core power-down, `SYSTEM_OFF`, cpufreq and the standalone (appended) boot are deferred.

## Alternatives

- **Chainload over the proxy (`chainload.py`), or the layer reading the ADT:** rejected; the layer would need an ADT parser, Apple boot-args handling and its own core start through RVBAR and PMGR, all of which m1n1 already does on every Asahi boot.
- **Payload appended to m1n1 for day 2:** rejected for now; it needs an m1n1 binary to append to (a build or a download) and a write to the stub volume. Kept as the later standalone form, same image.
- **Running under m1n1's own hypervisor:** not possible; the layer needs EL2 itself and the M1 has no nested virtualisation **[hypothesis: no FEAT_NV on this core]**.
- **Fixed link address on the M1:** rejected; which address is free depends on the m1n1 build and its heap state.
- **Upstream DTB as the primary input:** needs a kernel tree or a download on a hardware day; kept as the fallback.
- **Mask the guest timer with `IMASK` in its control register** (what m1n1's hypervisor does [verified: R:re/planB-feasibility.md]): rejected; the guest would see its own register change.
- **Untrapped list-register mode (`hw`) on the M1:** rejected because of SEIS.
- **A "return to m1n1" test mode** (restore m1n1's state and read a RAM log over USB): not planned; to reconsider if the display shows nothing and no serial kit is available (RISKS Q-10).
