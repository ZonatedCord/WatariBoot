# Project status

**Updated:** 2026-10-14 (session marco-01, night closed)
**Strategy (2026-10-14):** **Plan B adopted**, Windows dual boot on top of a minimal EL2 layer (`docs/adr/draft-plan-b-el2-layer.md`, `re/planB-feasibility.md`). The pure native route is closed: GIC mandatory (V2), parking in deadlock (V3, F-15).
**M0 (2026-10-15):** ✅ Watari EL2 boots in QEMU TCG at EL2, ERETs to EL1, handles HVC/PSCI (`el2/boot/run-qemu.sh`); vGIC distributor/redistributor model with 69 host tests (`make -C el2 test`); 57 register/bit values checked against the Arm specs (44 confirmed, 10 corrected, 3 open: `re/el2-m0-register-check.md`). Next: M1 (stage-2, vGIC wired into the layer, EDK2 at EL1).
**Current phase:** 0, the spike on the Windows ARM64 HAL and non-GIC interrupt controllers

## Milestones
- [x] Plan approved (`docs/PLAN.md`)
- [x] Repository created
- [x] Name: **Watari Boot**, with the fixed subtitle "Native Windows on Apple Silicon" (ADR-0006)
- [x] Design session (`docs/design/watariboot/`, ADRs 0001 to 0005)
- [x] NTASP analysis and audit: independent route (ADR-0007), bug reported with issue NT-for-ASi/apple_silicon_platforms_mu#23
- [x] Gate A, first round: A3, A5, A6 (PR #15). Discovery: the M1 timer is FIQ-only, see the draft ADR "timer FIQ strategy"
- [x] Gate A, second and third rounds: A1, A2, A4, A7–A11 (PR #22 and this round's PR). **Gate A complete.**
- [x] `Wack0/NtHalExts` lead read (README): the official API for interrupt controllers was **removed** by Microsoft before Windows 8; the extension works by looking up internal functions. See `re/f0-nthalexts-lead.md`.
- [ ] Study the code of `halextbcm2709.c`/`halextapi.c` (read-only, without copying the DDK headers).
- [x] **Gate B, B1+B2** (2026-10-13): QEMU 11.1.2, EDK2 `edk2-stable202608` built (ArmVirtQemu AARCH64, CLANGDWARF) and booted in QEMU up to the UEFI Boot Manager. List of what is installed and how to remove it: `docs/INSTALLED.md` + `tools/uninstall-watari.sh`.
- [x] **V0 (TCG):** the Windows 11 ARM64 Setup (official it-it ISO build 26300, SHA-256 in `WatariBoot-data/iso/SHA256.txt`) runs in QEMU on **our own** EDK2. Screenshot: `WatariBoot-data/logs/screens/tcg-c1.png`. Script: `tools/run-qemu-windows.sh`.
- [x] **HVF (solved 2026-10-14, F-08):** our EDK2 hung at "Start boot option" (`[Bds]BdsWait(5)`). Diagnosis in progress (an agent was trying the firmware bundled with QEMU, a RELEASE build, GICv2). **To resume here:** check `git status` on the local branch `marco/gate-b-v0` for any changes left by the agent.
- [x] **Night of 2026-10-14 (closed, PR #27, summary in `ralph/PROGRESS-night.md`; resume from F-11):** autonomous work planned according to `ralph/NIGHT.md` (N1 HVF, N2 automatic installation, N3 acpigen, N4 V1), on the branch `marco/notte-2026-10-14`, diary in `ralph/PROGRESS-night.md`. **N1 and N2 done** (Windows 11 installed automatically with HVF, desktop verified); N3 done; N4 build ok but Windows does not start with our tables (F-11), resume from there.
- [x] **V1** (2026-10-14): Windows boots with our MADT+GTDT (the GIC MSI Frame was missing, F-11 solved). `docs/img/v1-ok.png`.
- [x] **V2**: with the MADT without GICD Windows hangs, so it seems to require the GICD.
- [x] **V3**: Windows **uses the parking protocol** (CPU1's mailbox written, our stub jumps), then bugcheck `HAL_INITIALIZATION_FAILED (0x5C)` (F-15): a handshake problem to solve.
- [!] **V6**: inconclusive (F-13); method B requires building QEMU (Marco decides).
- **Reading:** secondary cores via parking are a real route (V3); the GIC, on the other hand, seems mandatory (V2), so Plan B remains the most likely. Summary in `docs/sessions/2026-10-14-marco-01.md`.
- [x] ~~Next: installing Windows in the VM~~ (done, N2) (evaluate the internal disk for speed), then V1 and V6.
- [ ] Phase 0: yes/no verdict on the HAL extension for the AIC
- [ ] Phase 1: prototype in QEMU
- [ ] Phase 2: EDK2 on the MacBook
- [ ] Phase 3: native Windows kernel
- [ ] Phase 4: desktop
- [ ] Phase 5: full power
- [ ] Phase 6: all Apple Silicon

## Blockers and needs
- ✅ **Time Machine backup confirmed** (2026-10-12, completed at 19:06). Destination: encrypted "Time Machine" volume on the external disk, with a 1 TB quota; automatic hourly backups. Excluded `~/Library/Containers/com.apple.Safari/Data/Library/WebKit/WebsiteData` (a file with a name too long that blocked the backup). **The rule stands:** before every download the user is still asked for confirmation.
- ✅ **Space for Gate B (75–115 GB):** the external disk is used, `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (about 450 GB free beyond the Time Machine quota). The disk must be plugged in; the folder is created on first use.
- Phase 0: the task on the Windows binaries was stopped by a safety filter. A decision is needed on how to proceed.
- DFU restore: ✅ solved. The user's partner has a Mac (Apple Configurator and a USB-C cable are needed). First try the **revive**, which does not erase data; the **restore** erases everything.
- Debug host (from Phase 2): a used Windows x86 laptop or mini PC is recommended (m1n1 + WinDbg). Alternative: Raspberry Pi 5 8 GB (minimum Pi 4 Model B 4 GB) with a USB-A→USB-C cable, but WinDbg still requires Windows.
- A **debug host** (a Raspberry Pi 5 or an old laptop) is needed from Phase 2.

## Preliminary verdict (A7, before the halextbcm2709 lead)
- **Updated after NtHalExts:** clean native ~5%, native with internal functions ~20%, Plan B ~50%, other ~25% (`re/f0-nthalexts-lead.md`). The A7 estimates are in `re/f0-preliminary-verdict.md`.
- The Plan B threshold is decided by the user (draft ADR `draft-phase0-preliminary-outcome.md`).

## For Ciani (if working today)
No new issues, to avoid sending notifications. Pick one of these, on a `ciani/...` branch and with **a single PR** at the end:
1. **Study of `halextbcm2709`** (read-only on GitHub): how it registers the interrupt controller in NT's HAL (callbacks, structures, IPI). Output: a section in `re/f0-nthalexts-lead.md`.
2. **`tools/acpigen` fixes** from A2 (`re/f0-acpi-windows-requirements.md`, discrepancies section): parking version 1 + address, boot CPU first, GICR=1, no placeholder GICD; then the FADT and DBG2 generators. Criterion: `make -C tools/acpigen test` green.

## To decide (non-blocking)
- **GitHub organization:** optional and postponable. The name is to be chosen (`watari` is taken, `watariboot` is free). It must be created from the browser, then the repo is transferred (history is preserved and the old address is redirected). Reserve the name before any public announcement.

## Where to resume
- Latest diary: `docs/sessions/2026-10-15-marco-01.md`.
- **Next: M1** (issue #50): wire the vGIC into Watari EL2 (stage-2, GICD/GICR traps, list registers, timer and SGI paths, PSCI CPU_ON), then boot our EDK2 at EL1 on top of the layer in QEMU TCG.
- Tests to run first: `make -C el2 test`, `el2/boot/run-qemu.sh`, `make -C tools/acpigen test`.
- The repository is **public** since 2026-10-15; open issues are unassigned on purpose.
- Failures and lessons learned: `docs/FAILURES.md` (read by all sessions).
