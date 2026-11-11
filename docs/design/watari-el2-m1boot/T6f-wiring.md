<!-- SPDX-License-Identifier: MIT -->
# T6f wiring: how `psci.c` calls the M1 PSCI module

Status: the module exists and is host-tested; `psci.c`, `start.S`, `main.c` are **not** edited yet (T6c owns the seam).
Files: `el2/include/wel2_cpu_start.h`, `el2/vgic/cpu_start.c`, `el2/tests/test_cpu_start.c`.
Claims: [verified: source] / [hypothesis].

## 1. Platform object

`plat_m1.c` owns one `struct wel2_cs g_cs` and the real `struct wel2_cs_ops`:

| op | M1 implementation |
|---|---|
| `write64` | `*(volatile uint64_t *)pa = v` (EL2 stage 1 maps the spin table, which lives in m1n1's `.bss`, as Normal cacheable; the stage-1 map of that page is a T6c item) |
| `dc_cvac` | `dc cvac, pa` (the reader runs with MMU and caches off: clean to PoC, not just PoU) |
| `dsb` | `dsb sy` |
| `sev` / `wfe` | `sev` / `wfe` |
| `wake_store` / `wake_load` | volatile store / load of `g_wake[idx]` (own cache line per core, 64-byte padded; both sides have the MMU on and are coherent: no cache maintenance) |
| `timers_reset` | `msr CNTV_CTL_EL02, xzr` and `msr CNTP_CTL_EL02, xzr` (needs `HCR_EL2.E2H = 1`; under `wel2.e2h=0` use the EL0/EL1 encodings the way `apple_fiq_init_cpu` does, T6e) |
| `mmio_write32` | volatile 32-bit store; the watchdog page must be mapped as Device-nGnRE in stage 1 (T6c) |

Init (boot core, after the FDT is parsed, before the guest starts):
`wel2_cs_init(&g_cs, &ops, (uint64_t)secondary_entry /* run-time address, adrp+add, PIE */, cpus, n, boot_idx)`
where `cpus[i] = { mpidr from /cpus/cpu@N reg, cpu-release-addr }` in node order (= vCPU index = PSCI index = stack index,
DESIGN 3.3). The same list comes from T6b's `struct m1_platform`; copy it, the module does not keep a pointer to it.
`boot_idx` = index of the core whose MPIDR matches `mrs mpidr_el1`.

## 2. `psci.c` calls (platform = M1)

`psci.c` keeps all argument checks (entry in guest RAM, 32/64-bit masking, `PSCI_MPIDR_MASK`). Differences:

- **MPIDR mask.** `PSCI_MPIDR_MASK = 0x000000ff00ffffff` rejects bit 31 (RES1) and bit 24 (MT). The spec says those bits are MBZ for
  CPU_ON/AFFINITY_INFO [verified: Arm DEN0022F.b 5.1.4], but the guest sees `VMPIDR_EL2 = 0x80000000 | ...` on the M1 (RES1 bit set),
  so a guest that passes `MPIDR_EL1` unmasked would be refused. Linux masks it (`MPIDR_HWID_BITMASK`); Windows' behaviour is
  [hypothesis]. Decide in T6c whether `psci.c` strips bits 31/30/24 before the check (module expects the masked value and refuses
  anything else, as tested in `refusals`).
- `cpu_on`: after the existing checks, replace `g_pcpu[idx].entry/ctx/state` bookkeeping and the `psci_smc(CPU_ON)` loop by
  `r = wel2_cs_cpu_on(&g_cs, target, entry, ctx);` and return `r` unchanged (same numeric codes as the `PSCI_*` macros).
  `INTERNAL_FAILURE` means no usable release address: log it. `g_pcpu[idx].state` may stay as a mirror or be replaced by
  `wel2_cs_affinity()`; do not keep two sources of truth.
- `cpu_off`: teardown as today (vgic reset, `pirq_vcpu_off`, `cpuif_cpu_down`, `gic_phys_cpu_down`), then
  `r = wel2_cs_cpu_off(&g_cs, me)` (DENIED for the last core: do the check **before** the teardown, as today's `others` loop does, so
  nothing is torn down for a refused call), `big_unlock()`, `dsb sy`, then
  `wel2_cs_park(&g_cs, me, 0)` (never returns until woken) and re-enter via section 3. No `smc`.
- `affinity_info`: `wel2_cs_affinity(&g_cs, target)`.
- `SYSTEM_RESET`: `cpuif_stats()`, log, mask DAIF, `wel2_cs_system_reset(&ops, wdt_base)`; if it returns -1 (no `apple,wdt` node)
  log "no watchdog" and fall through to the halt; else `wel2_cs_wait_forever(&ops, 0)`. `wdt_base` = `reg` of the first FDT node
  with compatible `apple,wdt` (T6b/T6c) [verified: DESIGN 6].
- `SYSTEM_OFF`: `cpuif_stats()`, print `SYSTEM_OFF: halted, hold the power button to switch off`, final checkpoint, `wel2_cs_wait_forever(&ops, 0)`.
  What the user sees: the last frame and text stay on the panel, the fans/CPU idle in `wfe`, nothing powers off; they hold the power
  button [hypothesis: the panel keeps the last image, DESIGN 7]. The other cores keep running whatever they were doing (guest cores
  that are in the guest are not stopped); on the M1 this is acceptable for first boot, a later milestone may park them with an IPI.

## 3. Re-entry after the park (`main.c`, `el2_secondary_main`)

First start and wake share the tail: `secondary_entry` (start.S) -> `s1_enable` -> `el2_secondary_main`. After the wake the core is already
in C with its stack, so the sequence is, for the woken core: `wel2_cs_park` returns (flag cleared, **timers reset first**, M-3) ->
`apple_fiq_init_cpu()` (T6e) -> vgic/cpuif per-core init as for a first start -> `wel2_cs_enter(&g_cs, me, &entry, &ctx)` -> `eret` to the
guest. `wel2_cs_enter` is also used by the first start. `el2_secondary_main` must take the big lock like today.

## 4. No `smc` on the M1

- `psci_smc()` and its `smc #0` move into `plat_qemu.c` with the QEMU forwarding; `plat_m1.c`, `cpu_start.c`, `wel2_cpu_start.h` contain none.
- Check to add once the M1 image links (not before: there is no M1 link target yet): `llvm-objdump -d` of the image built with
  `PLATFORM=m1` only (or with the QEMU platform object excluded) must not contain `smc`; `grep -n 'smc' el2/vgic/cpu_start.c el2/include/wel2_cpu_start.h`
  already returns only comments. Both go into `el2/boot/check-pie.sh`-style Makefile checks.
- Guest `SMC` is still trapped by `HCR_EL2.TSC` and served by the same `psci_handle` (DESIGN 6); that is a trap, not an `smc` issued by the layer.

## 5. Ordering notes (concurrency)

- All module entry points run under the layer's big lock, except `wel2_cs_park` (called after `big_unlock()`) which only touches the core's
  own wake flag through the ops.
- First start: store -> `DC CVAC` -> `DSB` -> `SEV` [verified: Linux v6.12 `arch/arm64/kernel/smp_spin_table.c:85-100`]. A `SEV` before the clean would
  let the waiting core read stale zero and go back to `wfe`; it would then sleep until the next event.
- Wake: entry/ctx stores, `DSB`, flag store, `DSB`, `SEV`. The parked core reads the flag before each `wfe` and clears it only after seeing it,
  so a `CPU_ON` that lands between "state OFF" and "core in wfe" is not lost (`wake_before_park` test).
- A released core cannot be released twice: the module refuses (`INTERNAL_FAILURE`) a second spin-table start; after the first start every restart goes by wake flag.
- Watchdog order follows m1n1 (alarm, count, ctl) [verified: m1n1 v1.6.1 src/wdt.c:56-58]; the task text listed count first. The effect is the same.
- Not covered by the host tests: memory-model effects, a core released while m1n1's loop is in non-WFE polling mode (the poll sees the store without `SEV`),
  and what the secondary does if `secondary_entry` is reached with m1n1's x0..x3 = 0 (no use of them).
