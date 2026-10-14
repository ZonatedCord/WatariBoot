# Gate B: autonomous loop (Ralph) from 2026-10-14

Marco is away: Opus works alone with a Ralph loop. Single branch: **`marco/f11-acpi`** (never main, never merge, never force-push). **A single PR** at the end, towards main, without merging.

## Rules (in addition to CLAUDE.md)
- Execution delegated to **Sonnet** subagents (`model: "sonnet"`). Opus verifies in person (screenshots with Read, tests re-run) before ticking.
- Downloads and installations: **none new** (only the QEMU, iasl, llvm, EDK2, ISO and Windows disk already present). No analysis of Microsoft binaries.
- The Windows disk always with `snapshot=on` in the tests. **Never two QEMUs on the same disk.** Before launching one, `pgrep -fl qemu-system`: if an agent's QEMU is still active, wait.
- Data only in `/tmp/watari-data` (= external disk). Everything new goes in `docs/INSTALLED.md`.
- **Every failure** in `docs/FAILURES.md` (F-NN format), every result in `ralph/PROGRESS-gateb.md`.
- Commit and push after every task. No comments or assignments on GitHub.
- **Budget:** the usage limits are close. Agents with focused prompts, at most 2 in parallel; stop if a task fails twice.

## Tasks (in order; one task per iteration, independent ones can be done in parallel)
- [x] **G1 · F-11 / V1.** *(succeeded: the GIC MSI Frame was missing, see F-11)* Windows reaches the desktop with our MADT+GTDT (`acpigen -m qemu-virt` via `WatariAcpiOverride`). An agent is already working on it (started at 09:05); **do not relaunch it** if `re/f11-acpi-comparison.md` is being written or a QEMU is active. Criterion: screenshot `logs/screens/v1-ok.png` with the desktop.
- [x] **G2 · V2.** *(Windows requires the GICD: F-14)* Like V1 but with the MADT **without GICD** (only GICC + GICR). Expected outcome: either way, document it. If Windows does not boot, capture the bugcheck (screenshot and serial) → F-NN entry. Criterion: outcome documented with evidence.
- [!] **G3 · V6.** *(inconclusive: the PPIs go back to Group 1 right after, zero FIQs delivered; see F-13; method B requires building QEMU, Marco decides)* The timer delivered as FIQ. Method "A" from `EXPERIMENT-F0.md` §4.4: a DXE driver (in `WatariQemuPkg`) that after `ArmGicDxe` puts PPIs 27 and 30 in Group 0, with `kernel-irqchip=off` under HVF. First verify (serial and QEMU trace) that the timer really arrives as FIQ, then boot Windows. Criterion: outcome (Windows boots and the clock advances / hang / bugcheck) documented with evidence. If Windows puts the timer back in Group 1, document it and prepare the QEMU patch "B" **without building it** (building QEMU requires a download: ask Marco).
- [x] **G4 · V3 (preparation).** *(build done: `--parking`, V3 test still to do)* Real parking protocol: parking code in EDK2 (`WatariQemuPkg`) for the secondary cores, MADT with parking v1, FADT without PSCI. Criterion: successful build and, if possible, a test on Windows with 4 CPUs. Outcome documented.
- [x] **G5 · Closing.**
  - update `docs/STATUS.md`, `docs/sessions/2026-10-14-marco-01.md` and the token counter;
  - a **summary for Marco** in `ralph/PROGRESS-gateb.md`;
  - open **one** PR `marco/f11-acpi → main` (without merging);
  - then emit the promise.
