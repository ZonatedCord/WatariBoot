<!-- SPDX-License-Identifier: MIT -->
# PMC0-backed virtual PMUv3 cycle counter (`wel2.vpmu=global,src=pmc0`)

Status: implemented on branch `m3a-pmc0`, host-tested, QEMU bench regression run (timer source); **not yet run on the M1**.
Code: `el2/include/wel2_vpmu_pmc0.h`, `el2/vgic/vpmu_pmc0.c` (pure logic), `el2/boot/catch.c` (hooks), `el2/boot/apple_sysreg.c` (accessors).
Written from register facts only; every claim is marked **[verified: source]** or **[hypothesis]**.

## Why PMC0
The M1 has no PMUv3 (`ID_AA64DFR0_EL1.PMUVer = 0xF` [verified: measured on our j293, `hacr56-pmu.md`]). HACR_EL2[56] routes the PMUv3 accesses to EL2, where the layer emulates them. The first source was CNTPCT scaled to a nominal 1 GHz: monotonic and cheap, but it is not a cycle count. Marco's decision (2026-11-16): the counter comes from the M1's own cycle counter, so a guest that measures cycles (Windows' timing loops, calibration) sees real core cycles. The timer source stays as the QEMU default and as the per-core fallback.

## Registers (all through `struct apple_sysreg_io`; QEMU never executes them)
| Register | Encoding | Used bits | Confidence |
|---|---|---|---|
| PMCR0 | `S3_1_C15_C0_0` | bit 0 = counter 0 enable; IMODE [10:8], IACT bit 11 (FIQ module's) | encoding + IMODE/IACT [verified: Linux v6.12 `apple_m1_pmu.h:22-30`, already in `wel2_apple_sysreg.h`]; bit 0 meaning **[hypothesis]** |
| PMCR1 | `S3_1_C15_C1_0` | bit 8 = counter 0 at EL0 (A64), bit 16 = counter 0 at EL1 (A64) | **[hypothesis]** (brief) |
| PMC0 | `S3_2_C15_C0_0` | the counter; 64-bit or 48-bit unknown | **[hypothesis]** |
| PMSR | `S3_1_C15_C13_0` | overflow status | not used by the layer (IMODE off); **[hypothesis]** |

`check-apple-enc.sh` pins the three accessors (`pmcr1` rd/wr, `pmc0` rd) to these encodings in the object code, so a typo cannot silently hit another IMPLEMENTATION DEFINED register. PMC0 has a read accessor only: there is no code path that can write it.

## What the layer already does with the Apple PMU (FIQ coexistence)
`vgic/apple_fiq.c` [verified: read in this tree]: `apple_fiq_init_cpu` reads PMCR0 and writes it back with `IMODE` and `IACT` cleared (also `UPMCR0.IMODE`); the FIQ handler reads PMCR0 and, if `IMODE == FIQ && IACT`, writes it back with those two fields cleared (and checks again; a PMCR0 that does not quiet is a "stuck" storm). It never sets a bit and never touches PMCR1, PMC0 or UPMSR other than reading.
Coexistence rules:
1. Our init is a read-modify-write that only ORs bit 0 (PMCR0) and bits 8/16 (PMCR1), and writes only when a bit is missing; it never touches IMODE / IACT. If iBoot / m1n1 left IMODE = FIQ, our init leaves it for the FIQ module to clear (it does so at its own init, which clears IMODE).
2. The FIQ module's clear is itself a read-modify-write, so bit 0 survives it.
3. Host tests run both orders against the fake register file (`t_init_with_fiq_module_both_orders`), plus a test that PMCR0 bits 30 / 20 / IMODE / IACT survive our init unchanged.
Counter 0 overflow with the interrupt mode off sets no FIQ; nothing depends on PMSR.

## Data flow
```
guest mrs PMCCNTR_EL0 --HACR[56]--> EL2 (EC 0x18, or EC 0x3F + AFSR1)  [catch.c: catch_handle]
   class PMU, global mode:
     core state PENDING/OK  -> p0 = mrs PMC0 (read only)
                               PENDING: p0 != sample at init ? OK : FAILED   (once, logged)
     state OK  -> wel2_vpmu_read(sh, core, g, p0):  gate; v = p0 + off; clamp; publish
     else      -> wel2_catch_read(g, ...)   (timer source: CNTPCT scaled, always running)
   writes of any virtual-PMU register -> wel2_vpmu_write: update the shared control state g
        (PMCR, PMCNTEN, PMOVS, PMINTEN, PMUSERENR, PMCCFILTR, PMSELR: read back as written, N = 0), then
        apply what it means for THIS core's counter (set / reset / freeze / resume)
```
The control state is the one shared `g_gpmu` struct as before. Per core: `struct wel2_vpmu_core {state, running, off, last, frozen, sample}`; shared: `struct wel2_vpmu_sh {flags, pub0, hiwater}` (under the big lock; the trap handler already runs under it; the init takes it for the shared part).

## Offsets
- Visible value while running: `PMCCNTR = PMC0 + off` (mod 2^64). Hardware PMC0 is never stopped, zeroed or written.
- `PMCCNTR` write v: `off = v - PMC0_now` (running) or `frozen = v` (frozen). `PMCR.C`: same with v = 0. These affect the calling core only (a per-core counter, as on a real PMUv3).
- Gate `running = PMCR.E && PMCNTENSET.C` (`run` token: always). The gate is global control state; each core applies it lazily at its next access: freeze stores the visible value, resume sets `off = frozen - PMC0_now`, so the frozen interval never counts and nothing jumps. Imprecision (documented): a core other than the one that wrote the control register freezes at its first access after the write, not at the write itself.
- The counter starts frozen (PMCR.E = 0 at reset) at its start value.
- Default is the architectural behaviour (counts only while enabled); the timer source keeps its old "always runs" behaviour, so a guest that reads PMCCNTR without ever enabling it still gets a clock there. If the hardware run shows Windows reads it without enabling, use `run`.

## Cross-core monotonicity (sync, clamp, hiwater)
PMC0s differ per core (not synchronised; P and E clusters have different, DVFS-varying frequencies) [hypothesis, from the brief].
- `sync` (default ON; `nosync` to turn off): at init a non-boot core starts at the boot core's **latest published** guest-visible value (`pub0`, set at the boot core's init and at every boot-core read under the lock): `off = pub0 - PMC0_now`. So a thread that migrates sees values of the same magnitude. Trade-off: after start the cores drift at their own clock rates; the boot core's `pub0` may be stale at the moment a secondary starts (it can lag the boot core's true value by the time since its last guest read), so a migration right at start may see a small step backwards that no per-core clamp can hide.
- Per-core clamp: a core never returns less than the last value it returned (a hardware reading below an earlier one, e.g. a counter reset behind our back, cannot make the guest value go back). Explicit guest writes are exempt.
- `hiwater` (default off): a global high-water mark; a core reading below it jumps forward to it (never stalls). Trade-off: a busy slow core is dragged up to the fastest core's value, so per-thread cycle deltas on that core are inflated while another core runs ahead. Off by default for that reason; a guest write moves the mark with it.
- 48-bit wrap of PMC0, if the counter is not 64 bits **[hypothesis]**: the code is modulo 2^64, a narrower hardware counter would wrap after hours (2^48 / 3 GHz ~ 26 h) and the clamp would then hold the old value; the hardware run must show the width (read PMC0 high bits).

## Fallback
Per core, at init: enable (above), read back PMCR0 / PMCR1: if the enable bits did not stick the core is `FAILED` at once. Else sample PMC0 across a 1 ms CNTPCT spin at EL2: advanced = `OK`; not advanced = `PENDING` (not failed: the EL1 enable bits may not cover EL2 **[hypothesis]**, and the layer's spin runs at EL2). At the first PMU access of the guest on that core PMC0 is read again: moved since init = `OK` (logged), same = `FAILED` -> the core serves PMCCNTR from the timer source (its values then differ in scale from the PMC0 cores: documented). Log lines `vpmu: ... cycles/ms`, `vpmu: src=pmc0 live` / `PMC0 did not move at EL2` / `falling back`; panel row prefix `SRC=PMC0 D=<cycles per ms>` (`SRC=PMC0(PENDING) D=..`, `SRC=TIMER`, `SRC=TIMER(fallback)`); final statistics list every core's source.
The panel prefix adds ~22 characters to the status row (`SRC=PMC0 D=3200000 MDCR STUCK=0x0 HACR=0x100000000000000 CATCH N=3 LAST=PMCCNTR_EL0` is 83 of the 96 kept); the framebuffer geometry is unchanged (`test_fb` still checks guest height >= 1080).

## What the hardware run must show
1. `vpmu: PMCR0 a -> b PMCR1 c -> d`: b = a | 1, d = c | 0x10100 (so the encodings and bits stuck); IMODE unchanged.
2. `PMC0 x -> y cycles/ms = n` with n plausible (~2e6..3.3e6 on the M1: P cores up to ~3.2 GHz, E cores ~2.1 GHz); else the state line says PENDING / fallback.
3. At the first guest PMU access: `PMC0 counts at the guest's EL: src=pmc0 live` (PENDING cores) - or the fallback line.
4. A guest that reads PMCCNTR twice sees a growing value; across cores, values of the same magnitude.
5. No FIQ from the PMU (`apple_fiq` PMU counters stay 0); the rung HACR0 `PMC0 verdict` line agrees with the rate.
6. Whether Windows enables the counter before reading (else `run`), and the PMC0 width.

## Hypotheses (all)
PMCR0 bit 0 = counter 0 enable; PMCR1 encoding and bits 8 / 16; PMC0 encoding, width, per-core and unsynchronised behaviour; whether counting at EL2 is covered by the EL1 enable; PMSR encoding; that PMC0 is not reset by the PMCR0 write; DVFS-varying rates; `M1N1Proxy.udelay` in the probe.
