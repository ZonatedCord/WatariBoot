# Watari Boot: instructions for Claude

**Language:** all repository documentation is written in English; always talk to Marco (the user) in Italian.

Goal: **dual boot** of Windows ARM64 on Apple Silicon, **without macOS underneath**, on top of a minimal EL2 layer (Plan B, adopted on 2026-10-14: `docs/adr/draft-plan-b-el2-layer.md`). We start from a 13" M1 MacBook Pro (t8103, j293) and then generalize to all Apple Silicon. The full plan is in `docs/PLAN.md`.

## ⚠️ BACKUP REQUIRED (absolute rule from the user)
**Before downloading or installing ANYTHING on the Mac** (toolchains, tests, ISOs, brew packages, large repos, anything), stop and **remind the user to make a full backup** (Time Machine). Proceed only after the user has **explicitly confirmed** that the backup is done. No exceptions, even for small downloads.

## Workflow (mandatory)
- **Opus = boss.**
  - Reasons, designs, breaks down the work, verifies results and talks to the user.
  - Never reports an agent's result without having checked it.
- **Sonnet = executors.**
  - Agent tool with `model: "sonnet"`.
  - They execute well-defined tasks and answer to Opus.
  - Independent tasks must be launched in parallel.
- The user talks only to Opus.
- A new subsystem first requires a design session with `morasca-agent` (`model: "sonnet"`), with output in `docs/design/`.

## Documentation (at the end of every session, no exceptions)
1. Write `docs/sessions/YYYY-MM-DD-<author>-NN.md` (one file per author, to avoid conflicts): goal, what was done, decisions (with links to the ADRs), verified results, open problems, next steps.
2. Update `docs/STATUS.md`: current phase, milestones reached, blockers, where to resume.
3. Every architectural decision goes in a draft `docs/adr/draft-<title>.md`; the number is assigned at merge.
4. Update the token and cost counter: `python3 -I tools/token-usage.py --autore <marco|ciani>`. It updates `docs/costs/<author>.json` and the table in the README.
5. Commit and push **on a branch**, then open a PR to `main`. Never push directly to `main` (see `docs/CONTRIBUTING.md`).

## Collaboration
- Two people work on the project (Marco + Ciani), plus Ralph. The rules on branches, PRs, issues and "hot" files are in `docs/CONTRIBUTING.md` and must always be followed.
- Ralph loop: only on its own `ralph/...` branch, one instance at a time, instructions in `ralph/README.md`.

## Constraints
- **Dual boot, minimal layer**: no macOS underneath, no "classic" VMs (Parallels/UTM). **Only** the minimal EL2 layer (vGIC, PSCI, timer, IPI) decided by Marco on 2026-10-14 is allowed; everything else must stay native (full-speed cores, real peripherals). QEMU is used only as a test bench.
- **No Microsoft binaries or code in the repository**: ntoskrnl, PDBs, ISOs and dumps go in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (external disk; it must be plugged in), outside the repository. Reverse-engineering notes go in `re/`, written in our own words and citing addresses, without pasting large blocks of decompiled code.
- Asahi code (MIT / GPL-2.0 / dual) is reused in accordance with its license, with a provenance header.
- Every technical claim must be marked as **verified** (with evidence) or **hypothesis**.
- Hardware safety: macOS and recovery are never touched. Disk changes go only through the Asahi installer, and a backup plus the user's confirmation are required before any operation on the disk or on boot.

## Structure
- `re/`: reverse engineering of Windows ARM64 (HAL, interrupts, HAL extensions)
- `edk2-apple/`: EDK2 platform AppleSiliconPkg
- `halext-aic/`: HAL extension for the AIC and timer
- `drivers/`: Windows drivers (ANS StorPort, DART, PEP, SPI HID)
- `tools/`: build, images, QEMU
- `docs/`: plan, status, sessions, ADRs, design
