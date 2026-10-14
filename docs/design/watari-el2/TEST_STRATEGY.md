# TEST_STRATEGY: Watari EL2

- Project: Watari Boot, subsystem Watari EL2 · Date: 2026-10-14 · Version: 0.1 · Status: draft
- Linked documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [RISKS](RISKS.md)
- Marking: **[verified]** verified, **[hypothesis]** hypothesis. The basic tools (QEMU 11.1.2 with TCG, EDK2 `edk2-stable202608`, Windows 11 installed in N2, script `tools/run-qemu-windows.sh`, GIC trace with `kernel-irqchip=off`) already exist in the repository [verified: STATUS, FAILURES F-08, F-11, F-13, F-15]. No test in this plan requires new downloads or installations.

## 1. Pyramid
```
              /\        L4  Hardware M3 (self-test + Windows on M1)      few, slow, manual with backup
             /  \       L3  QEMU TCG system (EDK2, Windows)              dozens, minutes-hours
            /----\      L2  QEMU TCG integration with test guest         ~30, seconds
           /      \     L1  Fidelity oracle (trace replay)               hundreds of reads, seconds
          /--------\    L0  Host unit tests (macOS, native clang)        hundreds, < 10 s total
```
Rule: every bug found at a high level generates a test at a lower level (like F-11 → MADT comparison).

## 2. L0 — host tests (the heart of TDD)
The core (`vgic`, `cpuif` logic, `vtimer`, `psci`, `fdt`, `s2` table construction) is portable C **without hardware access**: accesses go through `*_ops` and a small `sysreg` layer that on the host is an in-memory structure. It is compiled with the system clang, without QEMU.

| Module | What is tested | Known edge cases |
|---|---|---|
| `vgic` (distributor) | every register in INTERFACES §5: reset, read/write, set/clear (ISENABLER/ICENABLER, ISPENDR/ICPENDR, ISACTIVER/ICACTIVER), priorities by byte and by word, ICFGR, IROUTER, ID registers | write to reserved registers; byte access to a 32-bit register; INTID 1020-1023; SPI beyond the maximum; `IROUTER` toward a non-existent vCPU; `IRM=1` |
| `vgic` (redistributor) | `GICR_TYPER` with `Last` only on the last, `Affinity_Value`; `WAKER` (immediate ChildrenAsleep); SGI_base frame | 1 vCPU and 8 vCPUs; repeated `WAKER` write |
| `vgic` (delivery) | choice of the highest-priority INTID, priority preemption, pending→active→inactive, level vs edge, SGI/PPI/SPI, masking by `PMR`/`BPR` | equal priorities (the lower INTID wins), >8 pending interrupts (LR overflow), interrupt enabled while pending, disabled while active |
| `cpuif` (logic) | flush/fold between software state and 8 fake LRs; underflow; emulation of `IAR1/EOIR1/HPPIR1/DIR` and `ICC_CTLR` (SEIS read as 0) | EOImode 0 and 1; IAR with nothing pending → 1023; EOIR of a non-active INTID (ignored, log) |
| `vtimer` | timer firing → PPI 30/27 pending; mask at the source; re-arm at EOI if no longer firing; re-injection if still firing | timer disabled while pending; two timers together; EOI without firing |
| `psci` | all functions in INTERFACES §9, OFF→ON_PENDING→ON state machine, errors | double `CPU_ON`, on itself, non-existent MPIDR, entry outside RAM; `AFFINITY_INFO` during transition |
| `fdt` | node/property reading, `fdt_trim_memory`, in-place patch, `/cpus` rebuild, corrupted FDTs (inconsistent lengths) | FDT without `/memory`; `psci/method` string already `hvc`; buffer too small |
| `s2` | table construction (blocks vs pages), `unmap` of a region inside a block (split), Device-nGnRnE vs Normal attributes | unaligned region; overlap; last page of the PA space |
| `trap` | ESR decoding (EC, ISS, ISV, SAS, SRT, WnR), registration tables (overlaps → `E_BUSY`), default-deny | `ISV=0` → panic; sysreg not in table; unknown HVC |

**First test (TDD, step by step)**
1. Write `tests/host/test_gicd_typer.c`: creates a `vgic` with 4 vCPUs, reads `GICD_TYPER` and checks `0x0178001D` (value [hypothesis] to be confirmed with the oracle, see §4).
2. See it fail (the module does not exist: link error).
3. Implement the minimum (`vgic_new`, `vgic_mmio_read` for that offset only), make it pass.
4. Second test: `test_spi_enable_pend_ack_eoi`: enables SPI 40 (INTID), makes it pending, sets `PMR`, calls the `IAR1` emulation and expects 40, then `EOIR1` and expects the inactive state.
5. From here on every new function is born from a red test.

**Property tests (deterministic)**: `xorshift64*` pseudo-random generator with a **fixed seed written in the test and printed at every run**; random sequences of MMIO/IRQ/ack/EOI operations against a simple **reference model** (not the optimized implementation). Invariants checked after each step:
- an INTID appears in at most one LR;
- no INTID is simultaneously pending and active except under the GICv3 rules (pending+active allowed for level);
- the delivered INTID has priority ≥ any other enabled pending one not masked by `PMR`;
- after `EOIR` followed by `ICENABLER`, no re-injection.
Number of sequences per run: 10,000, maximum time 5 s.

**Coverage**: `llvm-cov`/`gcov` on the core, threshold **≥ 90% of lines** and 100% of register-decoding branches (the command and the threshold fail the local build).

## 3. L2 — integration on QEMU TCG with a test payload
`virtualization=on` makes the GICv3 with `ICH_*` and the maintenance interrupt available at EL2 in TCG [verified: PB §7: `arm_gicv3_cpuif.c`, `virt.c`]. Reference command [hypothesis]:
```
qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -smp 4 -m 2G \
  -nographic -serial mon:stdio -kernel build/wel2.elf -d guest_errors -D logs/qemu.log \
  -icount shift=auto,sleep=off   # only for deterministic-time tests
```
The test payload (`tests/qemu/guest-*.S`, written by us, ≤ 200 lines) prints a token on UART for each step, and the test checks the token flow (grep with timeout). Cases:

| Test | Verifies | Milestone |
|---|---|---|
| `boot-el2` | banner, `CurrentEL=2`, `E2H=1` set by the layer, stage-2 active | M0 |
| `inject-ppi` | PPI 30 injection → the guest executes `IAR1` (trap or HW), reads 30, `EOIR1` | M0 |
| `gic-probe` | the guest reads all GICD/GICR registers of INTERFACES §5-6 and compares with the expected values printed by the layer | M0-M1 |
| `psci-smp` | the guest powers on cores 1-3 with `HVC CPU_ON`; each core prints its own MPIDR; then `CPU_OFF` and `AFFINITY_INFO` | M1 |
| `sgi-ipi` | each core sends an SGI to every other with `ICC_SGI1R_EL1`; the destination counts receptions (N×(N-1) expected) | M1 |
| `spi-device` | a virtio device with real interrupts (`gic-qemu` backend) → vSPI → guest | M1-M2 |
| `timer-tick` | the guest programs `CNTP` at 10 ms and counts 100 interrupts in ~1 virtual second (declared tolerance) | M1 |
| `modes` | the same tests with `cpuif_mode=HW` and `TRAP_EMULATE`: identical results | M1 |
| `fault-injection` | access to the layer region; forced `ISV=0`; unknown HVC; unmapped MMIO → panic with dump and expected code | M1 |
| `linux-control` (optional) | a test Linux arm64 kernel (if already present locally; **not downloaded**) uses the vGIC and reaches the console | M1 |

**E2H host test**: the layer boot in QEMU sets `HCR_EL2.E2H=1` to reproduce the M1 [hypothesis: ARCHITECTURE §1.3]; a test verifies that access to the guest registers goes through `*_EL12` and not through `*_EL1`.

## 4. L1 — vGIC fidelity tests (anti-F-14)
Goal: no vGIC register that Windows reads must differ from what it saw on a working GICv3.

**Oracle.** The V1 boot of Windows 11 (acpigen MADT, QEMU GICv3, lock screen reached [verified: F-11]) is rerun with `-accel tcg` or HVF and `kernel-irqchip=off` with the QEMU GIC trace (`-trace 'gicv3_*'`, as in F-13/F-15 [verified]) and recorded in a file: each line = (time, CPU, access type, offset, size, value read/written). The trace file and its hash go in `WatariBoot-data/oracle/` (outside the repository: it contains only GIC register accesses, no Microsoft code, but it is large).

**Replay on host.** `tests/oracle/replay.c` reads the trace and repeats Windows' writes on our `vgic` (same vCPUs), comparing every read with the oracle's value. Classification:
- **static** (must always match): `TYPER`, `IIDR`, `TYPER2`, ID registers, `GICR_TYPER`, `ICFGR` of SGI/PPI, `IPRIORITYR` reread after write, `ISENABLER` reread after write, `WAKER`, `CTLR` (including RWP);
- **dynamic** (pending/active, `HPPIR`, `IAR`): not compared in the replay; covered by L0 and the reference model;
- **expected** differences (e.g. number of SPIs, addresses) are in an `oracle-allow.txt` list with rationale.
Criterion: 100% of the static registers read by Windows match; report with the table "register read by Windows / frequency" (also serves to order implementation priorities).

**Additional measurements from the trace** (answer open decisions):
- write frequency of `ICC_PMR_EL1` and of `IAR/EOIR` per second during boot and at rest → estimate of the cost of the `TRAP_EMULATE` mode (RISKS R-05). The trace via `kernel-irqchip=off` records MMIO accesses to the GIC; the `ICC_*` registers are system registers and must be counted with a sysreg trace (QEMU TCG: `-d` with `trace:` on the `ICC_*` registers, or the layer's counters in `TRAP_EMULATE` — the practical alternative) [hypothesis].
- list of CPU bring-up sequences (like F-15 T6) to verify that the vGIC accepts them.

**Negative fidelity tests** (from F-14): for each "Critical" register in INTERFACES a variant with the wrong value is made (manual mutation testing: permanent `RWP=1`, wrong `Last`, `ChildrenAsleep` fixed at 1) and it is verified in L2/L3 that the symptom is **detected by the layer's watchdog** (timeout with dump) instead of being a silent hang (NFR-9).

## 5. L3 — system on QEMU TCG
| Test | Steps | Criterion |
|---|---|---|
| `edk2-boot` (M1) | `-kernel wel2.elf`, pflash with `QEMU_EFI-watari.fd`, `-smp 4` | serial: GICv3 detected, `BdsWait` terminates, Boot Manager; 3 runs with the same key events |
| `edk2-reset` (M1) | from the Boot Manager, `Reset` | the layer logs `SYSTEM_RESET` and QEMU restarts; no double boot |
| `win-boot` (M2) | installed Windows disk (N2) with `snapshot=on`, `-smp 4`, `ramfb` | lock screen within the limit (§7); screenshot with `screendump` on a path without spaces (F-07); before declaring a hang a key is sent (`sendkey shift`, F-10) |
| `win-smp` (M2) | same | layer log: 3 successful `CPU_ON` + BSP; no bugcheck 0x5C |
| `win-soak` (M2) | 30 minutes of idling | 0 panics, exit counters stable (rate ≤ NFR-1 threshold) |
| `win-baseline` (M2) | Windows on TCG **without** the layer (original firmware) | reference time for the 1.5× criterion |

Diagnosing a hang: the layer's watchdog (timeout with no exits from a vCPU for T seconds, T configurable) prints the state; QEMU's gdbstub (`-gdb`) is usable **but alters the guest's timeouts** (F-15) and therefore only for post-mortem.

## 6. L4 — hardware (M3)
Executed only after confirmation of the backup and Marco's go-ahead (absolute rule). Prerequisite: debug host (RISKS R-07).
- **M3a self-tests** (test payload + layer, without EDK2/Windows):
  1. `aic-sw-irq`: AIC `SW_SET` on an IRQ not connected to anything [verified: F0 §3.4] → vSPI → the payload receives it; verifies mask/unmask and target.
  2. `ipi-fast`: fast IPI core→core for all 56 pairs.
  3. `timer-fiq`: guest physical and virtual timer → vPPI 30 and 27; verifies that the `CNTP_CTL` read by the guest does not show a modified `IMASK`; absence of FIQ storms (FIQ counter per second).
  4. `psci-8`: `CPU_ON` of the 8 cores with spin-table.
  5. `serror-probe`: targeted reads/writes of the `ICC/ICH` registers to verify trap variant B (R-01) without SError; any SError → classification and log.
  6. `s2-attr`: nGnRE MMIO write from the guest to a harmless register → no SError (verifies the nGnRnE hypothesis).
  7. `mem-protect`: guest access to the layer region → handled fault.
- **Performance measurements (NFR-1):** control guest (Asahi Linux if already installed, or the test payload) in two configurations: identity stage-2 and minimal traps vs. native; CPU-bound loop, real MMIO access loop, high-rate IRQs (`SW_SET` in a loop). Write the numbers in `docs/costs/` or in a measurements file of the subsystem. m1n1 hv can serve for read-only observations [hypothesis].
- **Windows M3b:** USB-C with USB disk/keyboard; criterion in PRD §8.

## 7. Reproducibility
| Element | Rule |
|---|---|
| Toolchain | clang version written in `el2/TOOLCHAIN.md` and checked at the start of `make` (fails if different); flags `-ffreestanding -fno-pic -mgeneral-regs-only -Wall -Wextra -Werror`; `-ffile-prefix-map` and no timestamps |
| Build | `make -C el2 repro-check`: two builds from different folders → same SHA-256 |
| QEMU | 11.1.2, full command in `tools/run-qemu-wel2.sh` (to be written); `snapshot=on` for the Windows disks; duration and seed recorded in the log |
| Time | for L2: `-icount shift=auto,sleep=off` where possible (deterministic guest time); for L3: declared tolerances and checks on events (not on duration) |
| Data | images identified by SHA-256 in `WatariBoot-data/…/SHA256.txt` (already in use for the ISO) |
| Seed | all random tests print the seed; `SEED=<n> make test` reproduces it |
| Environment | paths without spaces (F-07: `/tmp/watari-data`); no network use in the tests |
| Outcome | each test writes an outcome file with command, versions, input hashes and result |

## 8. What is NOT tested in QEMU (and how it is compensated)
| Limit | Compensation |
|---|---|
| Timer as FIQ and `VM_TMR_FIQ_ENA_EL2` | L0 with fake sysregs; L4 `timer-fiq` |
| AIC (does not exist in QEMU, F-06) | L0 with `pirq-fake` and with a register model of the AIC written in the tests (map 0x2000-0x4200); L4 `aic-sw-irq` |
| SEIS errata and real SErrors | L4 `serror-probe`; L2 simulates SErrors with software injection |
| Performance | only exit counters in QEMU (useful for relative comparisons, not absolute ones); real measurements in L4 |
| Fast IPI, PMU FIQ | L4 |
| Windows behavior on Apple peripherals | out of scope (drivers) |
An AIC model inside QEMU would be ideal but requires compiling QEMU: **not done without Marco's go-ahead** (F-13) and not on the critical path.

## 9. Exit criteria per milestone
Reported in PRD §8; each criterion has a numbered test here (M0: §2 + §3 `boot-el2`, `inject-ppi`; M1: §3 and §5 `edk2-*`; M2: §4 and §5 `win-*`; M3a: §6).
