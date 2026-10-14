# Lead: Wack0/NtHalExts (`halextbcm2709`)

- Date: 2026-10-13 · Source: https://github.com/Wack0/NtHalExts (README read by Opus via `gh api`; Unlicense; last push 2024-09).
- **Warning:** the repo contains Microsoft DDK headers and libraries (Windows 8 M2 era). **They must not be copied** into our repo (ADR-0005). Only `README.md` and the author's original code are read.

## What the README says [verified]
- The HAL extensions were written for **ARMv7 NT** (Windows 8.x ARM32) and are loaded through **CSRT** tables.
- `halextgit`: extension for the ARM generic timer.
- `halextbcm2709`: extension for the **BCM2709 interrupt controllers** (Raspberry Pi 2, **without a GIC**), "backported from rs1-era HAL".
- It also says: the HAL extensions for interrupt controllers *"were added in 789x but removed again in 799x"*. For later builds the extension **searches by pattern for the functions no longer exported** by the HAL and recognizes the data structures that change from build to build (`halextapi.c`). If something does not match, it bugchecks.
- Tested on builds 7957–8400 (Windows 8 pre-release), "should" work up to 9600 (8.1), but this has not been tried.

## What it means for us
1. The **official** mechanism "HAL extension = interrupt controller" existed briefly and was **removed** before Windows 8 RTM. It is consistent with A1 (no public example) and with A4 (the BCM was supported inside the HAL).
2. However **a third way exists** (besides "clean native" and "plan B"): a HAL extension that **modifies no Windows file**, but finds and calls undocumented internal functions to register a controller.
   - Pro: no hypervisor; Windows stays original on disk.
   - Con: fragile, may break with every Windows build; requires knowing the internal functions of the Windows 11 ARM64 HAL, i.e. **binary analysis**, which our method today excludes (ADR draft "Phase 0 experimental"); possible problems with PatchGuard or HVCI [hypothesis]; proven only on ARM32 and Windows 8.
3. The original code of `halextbcm2709.c`/`halextapi.c` must be **studied** (read-only): how an interrupt controller is structured for the NT HAL, which callbacks are needed, how IPIs and priorities are handled. Even if the APIs have changed, it is the closest model to what is needed for the AIC.

## Updated probabilities (Opus estimates, not measurements)
| Path | A7 (before) | Now |
|---|---|---|
| "Clean" native (official mechanisms only) | ~12% | **~5%** |
| Native "with internal functions" (NtHalExts style) | not considered | **~20%** |
| Plan B (EL2 layer presenting a virtual GIC) | ~53% | **~50%** |
| Other / blocked | ~35% | **~25%** |

## Decisions that belong to the user
- Accepting the third way requires **revisiting the rule "no analysis of Microsoft binaries"**: whether, how and with what legal limits (EU interoperability, Dir. 2009/24/EC art. 6).
- Fixing the threshold for plan B.
