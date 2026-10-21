# TEST STRATEGY: Device interrupt routing on the M1

- Project: Watari Boot, subsystem "watari-el2-devirq" (#62) · Date: 2026-10-21 · Version: 0.1 · Status: draft
- Linked documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [RISKS](RISKS.md) · [DESIGN-STATUS](DESIGN-STATUS.md) · parent: [watari-el2 TEST_STRATEGY](../watari-el2/TEST_STRATEGY.md)
- Everything in sections 1-4 uses tools already on the bench (clang, QEMU 11.1.2, EDK2 `edk2-stable202608`, the installed Windows VM disk with `snapshot=on`). **No download or install is required**; hardware steps (section 5) and building QEMU for #42 need Marco's Time Machine confirmation first (project rule).

## 1. Pyramid
```
          /\       L4  M1 hardware self-tests (E-HW0, E-HW1) and Windows on the M1      few, manual, after backup
         /  \      L3  QEMU TCG + Windows (MSI matrix, soak)                            minutes-hours
        /----\     L2  QEMU TCG + EL1 test payload (edge SPI, re-target, guard)         seconds
       /      \    L1  Oracle: QEMU's physical GIC / ITS as reference behaviour         per run
      /--------\   L0  Host unit and property tests (el2/tests)                         < 10 s
```
Rule inherited from the parent: every bug found at a high level gets a test at a lower one.

## 2. L0 host tests (`make -C el2 test`, new suites)
Harness: `el2/tests/test.h` (`CHECK`, `CHECK_EQ`, `RUN`, fixture `setup(n)`); add the new files to `TESTS` in `el2/Makefile`.

| Suite (new) | Module | What is tested | Edge cases |
|---|---|---|---|
| `test_intr_map.c` | `wel2-intr-map` | exhaustive loop over hwirq 0..1023: bijection wired<->vINTID, hole 736-767 unused, MSI pool 704-735 -> 928-959 only through `intr_msi_vintid`, `intr_gsiv == vintid`, values of section 6 of ARCHITECTURE (590 -> 622, 777 -> 809, 695 -> 727) | hwirq 703/704/735/736/895/896; vINTID 31/32/927/928/959/960; SoC without MSI pool |
| `test_msi_frame.c` | `wel2-msi-frame` | `MSI_TYPER` = 928<<16 \| 32; `MSI_IIDR` fixed; `MSI_SETSPI_NS` write of 928..959 sets the SPI pending (edge) once; write of 927/960/0 ignored + logged once; byte/half/word access rules; reset | write to a read-only register; 64-bit access; SPI disabled while pending |
| `test_pirq_level.c` | vGIC + `pirq-fake-aic` | the rule table of ARCHITECTURE 4.2 for level sources: event -> masked -> injected -> ack -> EOI -> unmask; line still high re-fires; silenced does not; disable while pending; disable while active then re-enable; EOImode 0 and 1 (unmask at DIR) | two sources at once; 3 sources on 2 LRs (LR overflow); source fires while active (pending+active) |
| `test_pirq_edge.c` | same | edge sources: unmask at injection; second edge while pending merges; second edge while active becomes pending+active; `SET_TRIGGER` effect emitted on `GICD_ICFGR` writes | ICFGR flip while pending |
| `test_pirq_target.c` | same | `GICD_IROUTER` -> `SET_TARGET` -> fake `TARGET_CPU`; `IRM=1`; re-target of all SPIs on vCPU reset (`CPU_OFF`); event for a target that is OFF stays masked and is unmasked at the next wake (F-21 regression) | 8 vCPUs; IROUTER to non-existent vCPU |
| `test_pirq_guard.c` | `pirq-guard` | unexposed hwirq: logged once, masked, counted; storm threshold with a fake clock; legacy IPI event type 4 treated as unexpected; allowed source at high but legal rate is untouched | threshold boundary; counter wrap |
| `test_pirq_prop.c` | all of the above | property test: xorshift64* with a **fixed seed printed at each run**, 10,000 random sequences of {event, ack, eoi, enable, disable, route, trigger} on 8 sources against a 60-line reference model; invariants: no source masked with line high and nothing pending/active (lost-interrupt invariant R-D4), at most one LR holds a vINTID, a deactivated level source re-fires iff the line is high | seed in the failure message; max 5 s |

Coverage: `llvm-cov` on the new files, threshold 100% of register-decoding branches in `msi-frame` and `intr_map`, 90% lines for the rest (same rule as the parent).

**First test (TDD, step by step)**
1. Write `el2/tests/test_intr_map.c` with one check: `intr_wired_vintid(&T8103, 590) == 622`.
2. See it fail (header and function do not exist: compile error).
3. Create `el2/include/wel2_intr_map.h` + `el2/vgic/intr_map.c` with the minimum, make it pass.
4. Add `intr_wired_vintid(&T8103, 704) == INTR_INVALID` (red), implement the hole (green).
5. Add the exhaustive bijection loop; from here every new function starts from a red test.

## 3. L2 QEMU TCG with the EL1 test payload (seconds)
Existing scripts keep passing (`el2/boot/run-all.sh`, both cpuif modes). New payload cases, added to `payload.c` and a new `run-devirq.sh`:

| Test | Verifies | Expected output token |
|---|---|---|
| `edge-spi` | `ICFGR` edge forwarded to the physical GIC; the payload asks EL2 (a test-only HVC) to set a physical edge SPI pending through the physical `GICD_ISPENDR`, and receives it exactly once per trigger, also when triggered twice while the first is still active | `edge spi N received x3` |
| `retarget` | route a PL031 SPI to core 3, `CPU_OFF` core 3, interrupt must move to core 0 and be delivered; `CPU_ON` core 3 again and route back | `retarget ok` |
| `guard` | enable a physical SPI that is not allow-listed (storm via PL031 alarm that the payload never clears) and check the layer masks it and prints one line, boot core keeps running | `guard tripped hwirq N` |
| `msi-frame-cpu` | payload reads `MSI_TYPER`, writes `MSI_SETSPI_NS` = 940, receives vINTID 940 (frame modelled, not passed through, only in a `wel2.msi=emul` boot) | `msi 940 received` |
| `modes` | all of the above in `hw` and `trap` cpuif modes | identical tokens |

Note on what QEMU can and cannot test: DMA writes from QEMU devices go to the address Windows programs and do **not** pass through stage 2, so on QEMU the real v2m device stays passed through (mode `wel2.msi=pass`) and the modelled frame is exercised only by CPU writes (`wel2.msi=emul`). The Apple port semantics (doorbell, index) are testable only on hardware or on the optional #42 machine.

## 4. L3 QEMU TCG + Windows (the MSI matrix, M2.x-1)
Script change: `run-m2.sh` gets `MSI=gicv2m|its|off` (replacing the `ITS` knob; `its=on` stays as an alias for `MSI=its`). Fixed knobs for reproducibility: QEMU 11.1.2, EDK2 `edk2-stable202608`, `-smp 4`, `-m 4G`, `snapshot=on`, `CPUIF` fixed per row, `TAG` per run, logs under `$WATARI_DATA/logs/m2/<TAG>/`.

| Run | Machine | Layer | Purpose | Pass / what we learn |
|---|---|---|---|---|
| R0 | `its=off` (control, as F-23) | none | Dump the MADT (`pmemsave`, search `RSD PTR `, as F-11): is there a GIC MSI Frame? Does the storm reproduce? | Answers Q-D1; if no frame, the F-23 storm is "no MSI controller", not a Windows INTx bug |
| R1 | `msi=gicv2m` | none | Does Windows boot without ITS when the frame exists? Does it use it? | Lock screen; MSI frame writes seen by `-trace` on the v2m device (or GIC trace with `kernel-irqchip=off`) |
| R2 | `msi=gicv2m` | layer, `hw` mode | Same through Watari EL2 (needs edge SPIs, step M2.x-2) | Lock screen; layer stats: MSI SPI arrivals > 0, NVMe INTx arrivals = 0 |
| R3 | `msi=gicv2m` | layer, `trap` mode | M1-like cpuif mode | same as R2 |
| R4 | `msi=off` | none | Reproduces #65 with `-trace` on NVMe INTx assert/deassert (planned check of #65) | Is INTx deasserted when Windows acks? |
| R5 | `msi=its` | layer | Regression baseline (today's M2) | Lock screen in about 4-5 minutes |

Evidence per run: serial log, screenshot series (send `sendkey shift` first, F-10), `WEL2_STATS` counters, run time against the R5 baseline (flag a run slower than 1.5 x baseline, parent rule). A run that hangs is diagnosed with the control run without the layer first (lesson of F-23).

Soak (with #63): 30 minutes idle and a login in R2 and R3; guard trips must be 0.

## 5. L4 hardware (after backup, M3a/M3b)
| Test | Needs | Measures | Decides |
|---|---|---|---|
| E-HW0 AIC facts | debug host (#40), m1n1 proxy | `AIC_INFO` IRQ count; WHOAMI vs MPIDR for the 8 cores (#55); does an unmasked-after-event level source with line low stay quiet; does an edge latch while masked; spurious zero events; `SW_SET`/`SW_CLR`; `TARGET_CPU` single-bit routing | Whether `pirq-fake-aic` matches the chip; thresholds of the guard |
| E-HW1 MSI data | PCIe port up via m1n1, an endpoint (Wi-Fi) | `PORT_MSICFG` with L2MSINUM = 5; `PORT_MSIADDR` = `0xFFFFF040` (frame + `MSI_SETSPI_NS`) and then `0xFFFFF000`, to learn whether the port matches the exact address or the page; `PORT_MSIBASE` = 0 then 928; the endpoint's MSI capability is programmed with data k and with 928 + k; read the AIC events and `PORT_INTSTAT` bits 18-19 | H-A, H-B or H-C (ARCHITECTURE 5.2); the doorbell match granularity; whether a DART in bypass lets the doorbell through |
| SW loop | EDK2 payload under the layer | `SW_SET` of an allow-listed hwirq arrives as the right vINTID, on each core, both cpuif modes, latency | M-3 of the PRD |
| Windows M3b | drivers | ANS and DWC3 interrupts delivered; guard trips 0; exit counters | M-4 of the PRD |
Safety: only AIC mask/target/SW_SET and port MSI registers are written; the watchdog block and DART registers are not touched by these tests.

## 6. Reproducibility rules
- Seeds: one fixed seed per property test, printed on every run and on failure.
- Fixtures: the host tests build the vGIC with `setup(n)`; the fake AIC takes an explicit event script; no wall clock (a fake clock is passed to the guard).
- Versions pinned: clang from Homebrew LLVM as in `el2/Makefile`, QEMU 11.1.2, EDK2 `edk2-stable202608`; machine type `virt` of QEMU 11.1.2 (the `msi=` default changed across machine versions per the QEMU docs, so every run states `msi=` explicitly).
- No test depends on the host machine path: data lives under `WATARI_DATA`.
- Windows state is not deterministic: timing assertions are relative to the R5 baseline.

## 7. Where each requirement is tested
| Requirement | Test |
|---|---|
| FR-D1, D10 | `test_intr_map`, acpigen GSIV test |
| FR-D2, D3 | `test_pirq_level`, `test_pirq_prop`, bench R2/R3 |
| FR-D4 | `test_pirq_edge`, `edge-spi` |
| FR-D5 | `test_pirq_target`, `retarget` |
| FR-D6 | `test_pirq_guard`, `guard` |
| FR-D7, D11 | `test_msi_frame`, `msi-frame-cpu`, R1-R3 |
| FR-D8, D12, D13 | E-HW1, M4 tests |
| FR-D9 | SW loop, `WEL2_STATS` check in every L2 run |
| FR-D14 | `test_vgic_dist` (`LPIS=0` on the t8103 config), regression R5 |
| NFR-D7 | `test_pirq_prop` lost-interrupt invariant |
