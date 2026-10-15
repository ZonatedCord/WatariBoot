# Watari Boot: instructions for Claude

**Language:** all repository documentation is written in English; always talk to Marco (the user) in Italian.

## Project
- **Name:** Watari Boot (渡, "to cross over"). Private repo `ZonatedCord/WatariBoot`.
- **Goal:** **dual boot** of Windows ARM64 on Apple Silicon, **without macOS underneath**, on top of a minimal EL2 layer ("Watari EL2", Plan B adopted on 2026-10-14: `docs/adr/draft-plan-b-el2-layer.md`). First target: 13" M1 MacBook Pro (t8103, j293); then all Apple Silicon. Full plan: `docs/PLAN.md`; current state: `docs/STATUS.md`.
- **Stack:** C11 (freestanding) + AArch64 assembly, clang/lld (Homebrew llvm); EDK2 `edk2-stable202608` (toolchain CLANGDWARF); QEMU 11.1 (HVF / TCG) as test bench; bash + Python 3 for tooling.
- **Status:** research done (Phase 0), QEMU bench working, Watari EL2 designed (`docs/design/watari-el2/`); next milestone: **M0**.

## Commands
- Environment (mounts check, `/tmp/watari-data` link): `source tools/env.sh` (the external disk "Arthur Fonzarelli" must be plugged in)
- Host tests (ACPI generator): `make -C tools/acpigen test`
- EDK2 build: `tools/build-edk2.sh` (stock ArmVirtQemu + HVF patch) · `tools/build-edk2-watari.sh [--fiq|--parking|--nogicd]` (our platform)
- Run: `tools/run-qemu-uefi.sh` (UEFI only) · `tools/run-qemu-windows.sh [--headless] [--tcg] [--unattend]` (Windows VM; window by default)
- Headless smoke test: `tools/test-qemu-windows.sh`
- Token/cost counter: `python3 -I tools/token-usage.py --autore <marco|ciani>`
- Everything installed/downloaded and how to remove it: `docs/INSTALLED.md`, `tools/uninstall-watari.sh` (dry-run unless `--yes`)
- Lint/format: none yet.

## Structure
- `re/`: research notes (Windows ARM64 HAL, interrupts, M1 FIQ/timer, NTASP, plan B feasibility)
- `docs/`: plan, status, sessions, ADRs, design (`design/watariboot/`, `design/watari-el2/`), `FAILURES.md`, `INSTALLED.md`
- `edk2-apple/WatariQemuPkg/`: our EDK2 drivers (`WatariAcpiOverride`, `WatariTimerFiqDxe`, `WatariParkingDxe`)
- `tools/`: `acpigen` (ACPI table generator + tests), build/run scripts, `autounattend/`
- `ralph/`: Ralph-loop prompts, task lists and progress logs
- `halext-aic/`, `drivers/`: placeholders (plan A, currently unused)
- Large data (EDK2 sources/builds, ISO, VM disk, logs): `/Volumes/Arthur Fonzarelli/WatariBoot-data/`, never in the repo

## Code conventions
- `SPDX-License-Identifier` header in every file (MIT for our code, BSD-2-Clause-Patent for EDK2 drivers).
- Write from specifications: **do not copy** code from KVM (GPL-2.0), m1n1 (GPL-2.0 since 2.0) or NTASP (`docs/adr/draft-el2-license.md`).
- Every technical claim is marked **[verified: source]** or **[hypothesis]**.
- Rebuilding EDK2 after changing `acpigen` headers: the build script cleans the module (F-12); keep it that way.
- Commit messages in English, imperative mood.

---

## Agent workflow (user rule, always applies)

### Roles
- **Opus = boss.** Reasons, designs, breaks down the work, verifies results, talks to Marco and gives the final go-ahead.
- **Haiku = scanning.** Read-only phases (file audits, searches, data gathering). Never edits.
- **Sonnet = execution.** Editing phases; each edit in an isolated copy (`isolation: "worktree"`, this is a git repo).
- Marco talks only to Opus. Independent tasks run in parallel.
- Opus reads only filtered, verified results, not the agents' raw transcripts.

### Verification
- Every finding is checked by an **independent verifier agent** (not the one that produced it) before being reported.
- Opus reports to Marco only what passed verification.

### Fix cycles
- Fix cycles stop after **two rounds without progress**. Then back to Opus with a summary of what was tried.

### Trivial decisions (which file, which model, retry or stop)
- The agent chooses **only among listed options** and gives a confidence from 0 to 1. Answers outside the list are discarded.
- Confidence ≥ 0.8: it proceeds on its own. Below 0.8: the decision goes up to Opus.
- An LLM's confidence is an estimate, not a calibrated probability: do not rely on it for irreversible choices.

### Design
- A new project or subsystem first gets a design session with `morasca-agent` (PRD, architecture, risks, ADRs in `docs/design/<subsystem>/`).

---

## ⚠️ Backup before downloading (ABSOLUTE rule)
- Before downloading or installing ANYTHING on the Mac (tests, toolchains, packages, ISOs, anything): stop, remind Marco to make a full backup (Time Machine) and proceed only after his explicit confirmation.
- This also applies to agents: their prompts must forbid downloads and installations until the backup is confirmed.
- Everything that is installed must be recorded in `docs/INSTALLED.md` and be removable with `tools/uninstall-watari.sh`.

## Irreversible actions
- Before deleting, overwriting, pushing or sending content to external services: look at the target and ask for confirmation, unless explicitly authorized.
- Report results honestly: if a test fails or a step is skipped, say so with the output.
- Hardware safety: macOS and recovery are never touched. Disk/boot changes go only through the Asahi installer, after a backup and Marco's confirmation.

## End of session
- Update the project documentation: session diary `docs/sessions/YYYY-MM-DD-<author>-NN.md` + status in `docs/STATUS.md`; new failures in `docs/FAILURES.md` (F-NN); architectural decisions as `docs/adr/draft-<title>.md` (numbered at merge); token counter.
- Commit and push **on a branch**, then a PR to `main` (never push to `main`, see `docs/CONTRIBUTING.md`).

## Project-specific notes
- **Collaboration:** Marco + Ciani (+ Ralph). Rules in `docs/CONTRIBUTING.md`. **Few GitHub notifications:** one PR per session/milestone, no unnecessary comments or reassignments.
- **Ralph loop:** only on its own `ralph/...` branch, one instance at a time (`ralph/README.md`).
- **Allowed architecture:** no macOS underneath, no "classic" VMs (Parallels/UTM). Only the minimal EL2 layer (vGIC, PSCI, timer, IPI) decided by Marco on 2026-10-14; everything else stays native. QEMU is only a test bench.
- **No Microsoft binaries or code in the repo**; no analysis or disassembly of Microsoft binaries (F-01, `docs/adr/draft-phase0-experimental.md`).
- **QEMU tests:** Windows disk always with `snapshot=on`; never two QEMUs on the same disk; for screenshots send one `sendkey shift` first (black screen = display sleep, F-10).
- **Plan B of the whole project** (only if everything fails): a "better CrossOver" clone.
