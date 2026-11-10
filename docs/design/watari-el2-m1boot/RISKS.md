# RISKS and open questions (watari-el2-m1boot)

- Project: Watari Boot, subsystem "watari-el2-m1boot" · Date: 2026-11-10 · Version: 0.1 · Status: draft
- Linked documents: [README](README.md) · [DESIGN](DESIGN.md) · [PLAN](PLAN.md) · [draft ADR](../../adr/draft-m1-first-boot.md)
- Probability and impact: L / M / H, the author's estimate **[hypothesis]** in every row. "Settled by" names the ladder rung (DESIGN 9) or the plan step that turns the item into a fact.

## 1. Risks

| ID | Risk | P | I | Why we think so | Mitigation | Settled by |
|---|---|---|---|---|---|---|
| R-1 | A guest `ICC_*` access on the real M1 does not trap as the layer expects under `ICH_HCR_EL2.TC/TALL0/TALL1/TDIR` (wrong EC/ISS, no trap, or UNDEFINED at EL1 because `ID_AA64PFR0_EL1.GIC` = 0) | L | **H** (no unmodified GICv3 guest is possible without it: DESIGN 5.4) | KVM enables exactly these traps on M1 cores [verified: L:arch/arm64/kvm/vgic/vgic-v3.c:616-636, 705-715]; we only proved list-register delivery [verified: HW] | guest checks G5/G6 report each register separately; the EL2 sync handler prints EC/ISS of anything unexpected instead of hanging | L2 |
| R-2 | The people see nothing: framebuffer blank or not where the FDT says after m1n1's shutdown, and no serial kit | M | **H** (a blind run teaches nothing) | m1n1 passes the same framebuffer to Linux's simplefb [verified: M:src/kboot.c:180-240]; what the panel shows after `fb_shutdown(false)` is [hypothesis] | checkpoints are painted from the first C code that has the FDT; rung L1 ends in a watchdog reset, which is visible by itself (the Mac reboots within a second); optional serial kit | L1 |
| R-3 | m1n1's `kboot_prepare_dt` rejects the minimal DTB | M | L (fails before anything runs) | not every fix-up was read (DESIGN 4.1) | rung L0 dry run; fallback upstream DTB with `--dtb` | L0 |
| R-4 | An unknown or left-over FIQ source storms EL2 (a source we do not know how to clear) | L-M | M | no register names the FIQ cause; known list is the same in Linux and m1n1 [verified: F0] | per-core clean-up at init, cause counters, spurious budget → fatal with a dump on screen | L1 (boot core), L4 (all cores) |
| R-5 | The maintenance FIQ cannot be silenced by the harvest in some list-register state (it is not maskable) | L | M | KVM needed special handling for it [verified: L:drivers/irqchip/irq-apple-aic.c:411-420, 887-890] | DESIGN 5.2 step 3: clear the enable bits, count, never loop; trap mode needs maintenance only for overflow | L2-L3 |
| R-6 | Position independence: a relocation type other than `R_AARCH64_RELATIVE`, or an absolute address used before relocation, slips in later | M | M (boot core dies before any output) | new build mode for this code base | build check on the ELF's relocation types in the Makefile; QEMU always runs relocated (link address 0) | PLAN S6 |
| R-7 | Cache incoherence at the MMU-off/MMU-on boundaries on real hardware: the spin-table release word, images written by the proxy, `.bss`/stacks, page tables written with the MMU off | M | M-H (random early failures) | QEMU TCG has no caches; the existing maintenance code is [hypothesis] on hardware [verified: R:el2/boot/README.md "Open points for the real M1"] | explicit clean/invalidate at every boundary (DESIGN 2.3, 3.3); rungs L1 and L4 isolate boot core and secondaries | L1, L4 |
| R-8 | A secondary enters the layer in a state we did not expect (m1n1 MMU on, different DAIF, not in WFE mode) | L | M | read from source, not observed [verified: M:src/smp.c:60-106, M:src/memory.c:588-634] | `secondary_entry` masks DAIF and assumes nothing; `CPU_ON` also sends a fast IPI; a core that does not report within a timeout makes `CPU_ON` fail visibly, never hang | L4 |
| R-9 | Proxy ABI mismatch between the proxyclient (`main` of 2026-11-10) and m1n1 v1.6.1 for `kboot_*` | L-M | L (fails at L0) | day 1 used only reads, SMP start and calls [verified: HW header] | L0 dry run; fall back to a proxyclient of the v1.6.1 tag (a download on the host: Ciani's confirmation) | L0 |
| R-10 | Guest RAM bookkeeping: a reserved range (m1n1, its stacks, the FDT, the ADT) leaks into the guest, or the stage-2 pool overflows on the real memory map | L | M | the j293 map has unaligned ends [verified: HW boot log] | 32 MB coarse rounding (DESIGN 4.3), host test with the j293-shaped FDT, pool exhaustion is already fatal with a message | PLAN S8; L2 |
| R-11 | Emulated GICD/GICR windows at `0xF_0000_0000` / `0xF_1000_0000` collide with something | L | M | below the 36-bit limit; not checked against the ADT | read-only ADT walk on day 2 before L2 (same method as HW row 7) | day 2, before L2 |
| R-12 | The watchdog reset sequence does not reset, or resets into recovery | L | L-M | sequence is m1n1's own reboot [verified: M:src/wdt.c:50-61] | if no reboot within 5 s the layer prints "reset failed, halted"; power button | L1 |
| R-13 | Repeated forced power-offs put the Mac into a recovery/diagnostic start | L | L | m1n1's proxy set-up resets a panic counter [verified: R:docs/FAILURES.md F-29]; semantics [hypothesis] | boot macOS between sessions; DFU revive is the last resort and is still unproven (F-29) | experience |
| R-14 | The serial kit is wanted but conflicts with the host's zero-footprint rule (`sudo`, build) or the cable is not SuperSpeed | M | L | [verified: D:hw/soc/serial-debug.md] | optional; framebuffer first; the people decide in T8 | people |
| R-15 | SError at EL2 from the M1's locally generated SEI in some list-register state, even in trap mode | L | M | SEIS = 1 [verified: HW row 3]; KVM's answer is the traps | SError vector prints `ESR_EL2`/`ICH_*` state; trap mode only on the M1 | L2-L3 |
| R-16 | The layer grows a second, untested code path per platform and QEMU coverage erodes | M | M | most M1 code cannot run on the bench | one binary, platform chosen from the FDT; host tests with fakes for every M1 module; the same guest binary on both platforms | PLAN rules |

## 2. Open questions

| ID | Question | Current answer | Who / when |
|---|---|---|---|
| Q-1 | Does every `ICC_*` trap behave on the M1 as on QEMU, and does `ICC_SRE_EL1` read as SRE = 1 at EL1 without a trap? | [hypothesis] yes (R-1) | L2 |
| Q-2 | Why did m1n1's EL1 call on cpu7 hang on day 2 with IRQs masked and a list register armed? | unknown; SError (SEIS) is a candidate [hypothesis] | L2 may explain it (the guest has vectors for every exception class and reports) |
| Q-3 | Does m1n1 accept the minimal DTB, and does it fill `/chosen/framebuffer` when the node starts as `status = "disabled"` with no `reg`? | [hypothesis] yes | L0 |
| Q-4 | Exact board compatible string m1n1 expects for an appended FDT (`apple,j293`?) | [hypothesis]; irrelevant for the proxy path | when path B is built |
| Q-5 | What does the built-in panel show after m1n1's framebuffer shutdown, and is the format really `x2r10g10b10` with the stride in the FDT? | [hypothesis] last image stays; format from m1n1's own mapping of depth 30 | L1 |
| Q-6 | Do Windows and EDK2 look at `ID_AA64PFR0_EL1.GIC`? | [hypothesis] at least EDK2 does; the TID3 override makes the question moot | T11 |
| Q-7 | Is anything mapped at `0xF_0000_0000`-`0xF_1100_0000` on the j293? | [hypothesis] no | day 2, read-only |
| Q-8 | `SYSTEM_OFF`: is there a register-level power-off, or only the SMC? | [hypothesis] SMC only; halt for now | M3b |
| Q-9 | Does the test guest need the cores at more than their boot frequency (no `cpufreq_init`)? | [hypothesis] no | L4 timing |
| Q-10 | Should the first boot also be offered in a "return to m1n1" mode (the layer saves m1n1's EL2 state, runs the guest on one core, restores and returns, so the host can read the RAM log ring over USB)? | not in the plan: it adds restore code used only for tests; reconsider if R-2 materialises and no serial kit is available | Marco, after L1 |
| Q-11 | Does the serial kit get approved for Ciani's Mac (R-14)? | open | Marco + Ciani, before day 2 |
| Q-12 | Real `CPU_OFF` (power-down and restart through RVBAR) and idle states | deferred; the park loop is enough for Windows to boot **[hypothesis: Windows does not require cores to lose power]** | later milestone |
