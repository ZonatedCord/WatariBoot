# T7 wiring: what `main.c` (and friends) must do for the test guest

Status: written by T7a. The new files below are complete and host-tested; the boot code does **not** call them yet (T6c was refactoring `main.c`, `stage1.c`, `cpuif.c`, `psci.c` at the same time). This note is the exact to-do for the T6c owner. Claims: **[verified: R:file]** read in the code, **[hypothesis]** not yet run.

## New files (T7a)

| File | Role |
|---|---|
| `el2/include/wel2_hvc.h` | the vendor HVC ABI shared by layer and guest: ids, return codes, `WEL2_LOG_MAX` 160, check numbers `WEL2_CHK_*`, `struct wel2_stats` |
| `el2/include/wel2_guest_info.h` | `struct wel2_guest_info` (PLAN S14), magic `WEL2GINF`, flags |
| `el2/include/wel2_hvc_vendor.h`, `el2/vgic/hvc_vendor.c` | host-testable handler: `wel2_hvc_vendor(ops, x[4])`, callbacks for range check, guest read/write, log sink, checkpoint sink, stats |
| `el2/tests/test_hvc_vendor.c` | host test (8 tests, 143 checks); in `make -C el2 test` |
| `el2/guest/` | the EL1 test guest: `start.S`, `main.c`, `gic.c`, `guest.h`, `linker.ld`, `Makefile` -> `build/m1guest.bin` |
| `el2/boot/run-m1guest.sh` | QEMU run (chain build + `-device loader`), added to `run-all.sh` for `hw` and `trap` |

## 1. Call the handler in `handle_hvc` (main.c)

`handle_hvc(struct frame *f)` today: logs the first 8 HVCs, then `if (imm == 0 && psci_handle(f, fid)) return;`, else NOT_SUPPORTED **[verified: R:el2/boot/main.c handle_hvc]**. Insert **before** the PSCI call, inside the `imm == 0` case:

```c
#include "wel2_hvc_vendor.h"
extern const struct wel2_hvc_ops g_hvc_ops;     /* built once, see section 2 */

    if (imm == 0 && wel2_hvc_vendor(&g_hvc_ops, f->x))   /* f->x[0..3] in/out; returns false when not a vendor id */
        return;
```

`f->x` is `uint64_t x[31]`, so `f->x` is directly the `uint64_t x[4]` argument (only 0..3 touched). On a vendor id the function sets x0 = result and x1..x3 = 0 (SMCCC). Everything outside `0xC6000000-0xC600FEFF` is untouched and falls through to PSCI.

Also add `hvc_vendor.o` (`../vgic/hvc_vendor.c`) to `OBJS` in `el2/boot/Makefile` (the host build already has it in `LIBOBJ`).

Caveats:
- The existing `g_hvc_log_budget` trace (first 8 HVCs, "HVC from EL1, imm=...") should skip the vendor ids, or the guest's own log lines are mixed with its trace noise on the UART. In the QEMU run today the 8 budget lines show `fid=0xc6000002/3`.
- `handle_hvc` runs under `big_lock()`; the handler may therefore use the UART and the framebuffer without more locking. The guest's secondary cores log concurrently only through this path.

## 2. The `ops` table (new code in `main.c` or a new `hvc_glue.c`)

| callback | implementation |
|---|---|
| `range_ok(ctx, pa, len)` | `[pa, pa+len)` inside the Normal stage-2 RAM blocks and **outside** every hole: reuse `ram_map` / `maps` (the same ranges as the Normal blocks, `el2/include/wel2_ram.h`, `wel2_maps.h`), i.e. not the carve-out, not GICD/GICR/MSI/AIC/UART windows. Must be overflow safe (the handler already rejects `pa + len < pa`). |
| `read_guest(ctx, pa, dst, len)` | `dcache_clean_range`-style **clean+invalidate (`dc civac`) of `[pa, pa+len)` first**, then copy. The guest runs with MMU and caches off (Device memory), so its stores are in memory while EL2's cacheable map may hold stale lines **[hypothesis: needed on the M1; harmless on QEMU TCG]**. Copy with the layer's byte-safe `memcpy` (`-mstrict-align` flavour is fine). EL2 reaches guest RAM by identity (IPA = PA, EL2 stage 1 maps RAM) **[hypothesis: check `stage1.c` that the whole guest RAM is mapped at EL2]**. |
| `write_guest(ctx, pa, src, len)` | copy, then `dc cvac`/`dc civac` of the range + `dsb sy` (the guest reads with caches off). Used only by `WEL2_STATS`. |
| `log_line(ctx, line, n)` | one line to **all sinks**: UART (`puts_`), framebuffer text (`puts_` already mirrors to `g_fb`) and the log ring if there is one. Suggested format `"Watari EL1: " + line + "\n"` (the same prefix the existing payload uses and the guest's QEMU fallback prints, so `run-m1guest.sh` and T8's expected lines are identical in both paths). `line` is not NUL terminated; it is sanitised ASCII (no CR/LF), `n <= 160`. |
| `checkpoint(ctx, n, state)` | `checkpoint_state(WEL2_CHK_SQUARE_BASE + n, state)` (util.c; `0` running / `1` pass / `2` fail, matches `WEL2_CHKSTATE_*`). The handler has already logged `guest checkpoint N running\|PASS\|FAIL` through `log_line`. |
| `stats_fill(ctx, st)` | fill `st->cpuif_mode` (`WEL2_CPUIF_HW/TRAP/TRAP_NOTC`) and `st->count[WEL2_STAT_*]` from the trap counters of `cpuif.c`: one counter per ICC register in `cpuif_sysreg_trap` (SRE, IGRPEN1, BPR1 = TALL1 group; PMR, CTLR, RPR = TC group; IAR1, EOIR1, DIR; everything else in OTHER). They are system wide; the guest reads them before and after a few accesses of its own, with secondary cores idle. Pass NULL to leave `WEL2_STATS` at NOT_SUPPORTED (G5a/G5b then print `counters n/a` and judge values only). |

## 3. Guest info block and entry (PLAN S14, `chain_to_edk2` generalisation)

- Fill `struct wel2_guest_info` in guest RAM (aligned 8, e.g. just below the entry or at the FDT hand-off buffer end) and pass its address in **x0** instead of the FDT. Fields: `magic` `WEL2_GINFO_MAGIC`, `version` 1, `size`, `platform` ("qemu-virt" / "t8103"), `gicd_base`, `gicr_base`, `gicr_stride` 0x20000, `ncores`, `checks` (bit n = `WEL2_CHK_*`, 0 = all; from `wel2.step`), `cpuif_mode`, `mpidr[]`, and `flags`:
  - `WEL2_GINFO_F_PAUTH` **only when the layer really set `HCR_EL2.API|APK`** (S13). Without the info block the guest does **not** touch the PAuth keys: on QEMU today the key write (`msr APIAKeyLo_EL1`) trapped to EL2 with EC 0x18 and killed the run (`UNEXPECTED EXCEPTION ... ESR 0x62300942`), which confirms `g_hcr` lacks `API|APK` **[verified: HW-free QEMU run]**. With an info block but without this flag and PAuth reported, G3 FAILs by design.
  - `WEL2_GINFO_F_NO_PTIMER` while the P gate (PPI 30) is not implemented: G8 is then SKIPped. Without an info block the guest SKIPs G8 itself when it sees no INTID 30 at all (QEMU today: the layer injects only PPI 27 **[verified: QEMU run, G8 gets 0 ticks]**).
  - `WEL2_GINFO_F_POWEROFF` to end with `SYSTEM_OFF` instead of `SYSTEM_RESET`.
- Entry: `wel2.guest=<hex>` selects the entry; the guest has an arm64 Image header (`ARM\x64` at +0x38, `image_size` = image + .bss + 8 stacks of 8 KB, about 106 KB) so the existing check in `chain_to_edk2` accepts it unchanged. **The guest is position independent and needs a 4 KB aligned load address**; the host loader must reserve `image_size` bytes (the header value, not the file size: `.bss` and the stacks follow the file). On QEMU `run-m1guest.sh` loads it at `0x40280000` = `EDK2_ENTRY`, no edit needed.
- Secondary cores: nothing to add. The guest's `CPU_ON` passes `entry` = its own `secondary_entry` and `context_id` = the core's stack top; `psci.c` already `eret`s with x0 = context **[verified: QEMU run G9/G10]**.

## 4. What passes today (QEMU, nothing wired) and what needs the wiring

`run-m1guest.sh` (guest falls back to the PL011 when `WEL2_LOG` answers NOT_SUPPORTED; identical text) passes G1, G2, G3 (without the PAuth sign/auth), G4, G5a/G5b (values only), G6, G7, G9, G10, G11 in `hw` and `trap`; G8 prints SKIP. Needing the wiring:

| Check | Needs |
|---|---|
| G1-G11 output through the layer | section 1 + 2 (`log_line`, `checkpoint`); until then the PL011 fallback carries it |
| G2 `wel2_version=...` | section 1 (currently `n/a`, accepted) |
| G3 PAuth sign/authenticate | S13 `API\|APK` + info flag `PAUTH` |
| G5a/G5b counter judgement, including the `TC` negative control | `stats_fill` + the `trap-notc` mode (S12b) |
| G8 | P gate for PPI 30 (S9) |
| M1: any output at all | section 1 + 2 (the stage-2 map has RAM only; the PL011 fallback is disabled when an info block is present) |

## 5. Decisions of T7a

- Static link at 0, no relocations (checked in `el2/guest/Makefile`), instead of a self-relocating PIE: all references are `adr/adrp`, the check tables avoid function pointers and initialised pointers. Reason: the layer's load address on the M1 is chosen by the host loader and only 4 KB alignment is needed.
- MMU and caches stay off in the guest (as the existing payload): `-mstrict-align`, `-mgeneral-regs-only`, no exclusives, secondary cores are started one at a time so their log lines never interleave.
- Check numbers `WEL2_CHK_*`: G1..G4 = 1..4, G5a = 5, G5b = 6, G6..G11 = 7..12 (squares 17..28).
- The task text listed the checks in a different order (e.g. G4 = timer, G7 = PSCI); the guest follows DESIGN section 8 exactly.
- G11 is judged before the call (no return is expected): its verdict is "no unexpected exception, no rejected LOG call"; the proof of the reset is the layer's `PSCI SYSTEM_RESET` line and QEMU exiting with `-no-reboot`.
