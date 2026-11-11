# T6e wiring note: where the Apple FIQ module plugs in

- Status: host-testable part done (`el2/vgic/apple_fiq.c`, `el2/include/wel2_apple_fiq.h`, `el2/include/wel2_apple_sysreg.h`, `el2/boot/apple_sysreg.c`, `el2/boot/check-apple-enc.sh`, `el2/tests/test_apple_fiq.c`). Nothing in `el2/boot/main.c`, `cpuif.c`, `start.S` or `boot/Makefile` was touched, so the QEMU images are byte-for-byte what they were. This note is for T6c/T6d/T6f/T6g once `g_plat` exists.
- Marking: [verified: R:file:line] = read in this tree at the time of writing.

## 1. Build

- `boot/Makefile`: add `$(OUT)/apple_fiq.o` (from `../vgic/apple_fiq.c`, no `STRICT`) and `$(OUT)/apple_sysreg.o` (from `apple_sysreg.c`) to `OBJS`, and `../include/wel2_apple_fiq.h ../include/wel2_apple_sysreg.h` to the `%.o` dependency list. Both are plain C; `apple_sysreg.c` uses only `mrs`/`msr`/`isb`, no FP (`-mgeneral-regs-only` is fine). They are only *called* when the platform is the M1; on QEMU they are dead code (the image grows by a few hundred bytes).
- `make -C el2 test` already runs `boot/check-apple-enc.sh` (21 accessors, one mrs/msr each, compared to an independent table). Keep `apple_sysreg.c` the only file with Apple encodings.

## 2. State

One `struct apple_fiq_cpu` per core (put it in `g_pcpu[]` or next to `struct cif`): `io = &apple_sysreg_real`, `vgic = g_v`, `vcpu = this core's vCPU index` (the same `me` that `cpuif.c` passes to `vgic_set_irq_level`), `on_ipi` = optional (the flush that follows every FIQ already delivers; NULL is fine), `spurious_budget = 0` (default 16).

## 3. Entry points and where they are called

| Call | Where | Replaces |
|---|---|---|
| `apple_fiq_init_cpu(&c)` | M1 platform `discover/init` on the boot core (checkpoint 4 of S13) and on every secondary / park re-entry (`el2_secondary_main`, T6f), always before the `eret` and before `ICH_HCR_EL2` is enabled | nothing on QEMU |
| `apple_fiq_close_gates(&c)` | M1 `cpu_park` (T6f) as its first step; `CPU_OFF` | nothing on QEMU |
| `apple_fiq_handle(&c)` | `el2_exception`, `idx == 10` (lower EL AArch64 FIQ) -> `g_plat->fiq()`; today index 10 falls into `fatal()` [verified: R:el2/boot/main.c:481-487]. After it returns: `cpuif_flush(); apple_fiq_after_flush(&c); cpuif_kick_others();` (the same tail as `cpuif_irq`). If it returned `APPLE_FIQ_STORM`: `fatal_msg("FIQ storm")` with a register dump (it has already masked every source, so the dump itself cannot be interrupted) | `fatal()` for index 10 |
| `apple_timer_resync(&c)` | `cpuif_flush()` at the "Level-sensitive virtual timer" block [verified: R:el2/boot/cpuif.c:344-351]: the three lines `line = vtimer_line(); vgic_set_irq_level(...); if (!line && c->vtimer_masked) { ...gic_phys_ppi_enable(PPI_VTIMER, true); }` become `g_plat->timer_resync(me)`. QEMU keeps the old body (`vtimer_line()` + physical PPI), the M1 body is `apple_timer_resync()`. It also handles PPI 30 (EL1 physical timer), which the QEMU path does not | physical-GIC use 1b (unmask) |
| `apple_ipi_send(&c, target_mpidr)` | `cpuif_kick_others()` -> `gic_phys_kick(j)` [verified: R:el2/boot/cpuif.c:388] becomes `g_plat->kick(j)`; on the M1 it needs the MPIDR of core `j`, which `g_cores[j].mpidr` (FDT, T6b) holds | physical-GIC use 3 (SGI kick) |

## 4. The three physical-GIC uses in `cpuif.c`

1. **Timer PPI mask/unmask:** mask is `cpuif_irq`'s `PPI_VTIMER` branch (`c->vtimer_masked = true; gic_phys_ppi_enable(PPI_VTIMER, false)`, [verified: R:el2/boot/cpuif.c:450-455]); unmask is the flush block of section 3. On the M1 the mask is done by `apple_fiq_handle()` itself (it closes the gate bit and raises the vGIC line), and the unmask by `apple_timer_resync()`. The old `c->vtimer_masked` flag is replaced by `apple_fiq_cpu.vmasked/pmasked`; `vtimer_masked = false` at `cpuif_vcpu_reset()` [verified: R:el2/boot/cpuif.c:295] becomes `apple_fiq_close_gates()` + the per-core init when the vCPU is (re)started.
2. **Maintenance PPI 25:** the `PPI_MAINT` branch of `cpuif_irq` (`mrs ich_misr_el2`, stats, `cpuif_flush()`, [verified: R:el2/boot/cpuif.c:470-477]) is, on the M1, the `APPLE_FIQ_MAINT` cause of `apple_fiq_handle()` + the flush + `apple_fiq_after_flush()`. The statistics `st_maint` / `st_uie` can be fed from `c.st.maint`; `st_uie` needs the `ICH_MISR_EL2.U` bit, which the module does not split out (read it in the flush if wanted).
3. **SGI kick:** section 3, last row.

## 5. Things the integrator must keep in mind

- `apple_fiq_after_flush()` writes `ICH_HCR_EL2` behind `cpuif.c`'s cached value `c->hcr_cur` [verified: R:el2/boot/cpuif.c:237, 297 `hcr_cur = ~0ULL` is the invalidation idiom]: set `hcr_cur = ~0ULL` when it returns `true`, otherwise the next `hcr_set()` may think the enables are still on. A quenched UIE comes back with the next `hcr_set(c, overflow)` if the overflow condition still holds; the counter `st.maint_quenched` makes a persisting storm visible.
- The module never writes `CNTV_CTL_EL02` / `CNTP_CTL_EL02` except to `0` in `apple_fiq_init_cpu()`. `cpuif.c`'s `vtimer_line()` must not be used on the M1 (it reads the same register but has no P timer and no gate).
- `apple_timer_resync()` is only meant for a running vCPU. After `apple_fiq_close_gates()` (park) do not call it until `apple_fiq_init_cpu()` ran: with a firing timer it would "pre-mask" a gate that is closed on purpose and later reopen it.
- FIQ is taken at EL2 with `HCR_EL2.FMO = 1` (already in `g_hcr` for M1) [verified: R:docs/design/watari-el2-m1boot/DESIGN.md 5.1]; EL2 itself runs with DAIF masked, so a FIQ raised during the handler is taken after the `eret`. That is why `apple_timer_resync()` closes the gate of a timer it sees firing before the `eret`.
- Open [hypothesis] items that only the hardware settles: that the gate bit closing is effective with a single `isb` (the accessor issues one after every write, as Linux does for its `fiq_set_mask`); that `IPI_SR_EL1` really is write-1-to-clear on the P cores as on the E cores (Linux writes `IPI_SR_PENDING` to clear it on every core [verified: Linux v6.12 irq-apple-aic.c:787, 830]).
