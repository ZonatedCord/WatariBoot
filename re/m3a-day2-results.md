# M3a day 2: first boot of the Watari EL2 layer on the real M1 (j293)

(Name: `re/m3a-day2-results.md`; PLAN S18's `re/m3a-day2-first-boot.md` is superseded.)
- **Date:** 2026-11-13. **Who:** Marco (target) + Ciani (host), guided by Claude on the host, procedure `docs/procedures/m3a-day2-hardware.md`.
- **Target:** MacBook Pro 13" M1 (j293 / t8103); macOS 26.5.1 (as on day 1); m1n1 **v1.6.1** (console line `m1n1 v1.6.1`), Asahi "Tethered boot" volume from day 1, reused unchanged.
- **Host:** MacBook Pro M4 (Mac16,1), macOS 27.2.1; the day-1 USB-C data cable, target side on the left port nearest the hinge; photos and videos with a phone, transferred into `$HOSTDIR/work/photos/` (not committed).
- **Commit of the bundle:** `faa9cff` on `ralph/m3a-night` (content identical to `main` `235f726`, PR #82). SHA-256: `wel2-m1.bin` `facaeca15cd979bdf2ab37427b659f18754191f3e37158034b975a54cac7037b`, `wel2-chain.bin` `38dcf63d30da4ad2573d527d78337bec0ab72e5ee61ce3dd2c93efd9dd893496`, `m1guest.bin` `c176245bba969518d1ada044c943881c6b7d8f45ecb14616fe9d420d71140b03`. Gate on Marco's Mac: `run-all.sh` 29 PASS / 0 FAIL, `make -C el2 test` and the loader tests green, `shasum -c` OK; on the host `shasum -c` OK (40 files) and identical hashes. T7b included: yes.
- **Proxyclient:** m1n1 `main` source archive of 2026-11-13, sha256 `24cb82107417b4dee4b5f5699ff830284143b6d2763e46aa93f137db0e266e8c` (newer than day 1's; see F-32 for the `brew` call at import). Python 3.12.2 venv.

## Verdict
- **Go/no-go (L3 passes G1-G6 and reaches G11 in trap or trap-notc): GO.** **[verified: photos L3-trap-2, L3-notc-1-final, 2026-11-13]**
- Configuration that passed: **both** (`trap` and `trap-notc`).
- Default for the M1: `wel2.cpuif=trap` (decision table row 1: `TC` behaves too).
- The whole ladder L0-L5 passed; FAIL 0 at every rung.

## Per rung

### L0
- `--rung L0`; exit 0. First attempt stopped before any proxy call (F-32: the new proxyclient ran `brew --prefix llvm` at import and timed out); rerun with `PATH` limited to the venv and system directories.
- **PASS:** `kboot_prepare_dt OK`; `platform m1 (apple,aic found)`; 8 cpus, `reg` 0x0-0x3 and 0x10100-0x10103, `release_addr` set for cpu 1-7; memory 0x802078000..0x9df45c000 (+ one 16 KB range at 0x9df460000, wholly reserved); aic 0x23b100000 size 0xc000; uart (s5l) 0x235200000 size 0x4000 chosen by stdout-path/serial0; wdt 0x23d2b0000 size 0x4000; **framebuffer 0x9e2570000, 2560x1600, stride 10240, x2r10g10b10**; layer block 0x810000000 and guest block 0x812000000 inside usable RAM: **`BLOCK CHECK: PASS`**; nothing in the fdt dump at the R-11 windows 0xF_0000_0000 / 0xF_1000_0000.
- m1n1 warnings (expected with the minimal DTB): AIC affinities node, `/cpus/cpu-map`, gpu, sep, pmp, nvram, isp not found.
- Bundle `fdt_m1_dump` was killed (exit -9) by Gatekeeper because it arrived by AirDrop (quarantine); `xattr -d com.apple.quarantine` on that file only, with Ciani's OK (procedure 2.2).

### L1 (twice)
- `--rung L1 --go --boot-timeout 15`; exit 0 both times. Layer at 0x810000000, `wel2.step=1`.
- **PASS twice:** handed over at 09:35:42 and 09:38:47; USB gone at once; **the Mac restarted in under 5 s** both times (people's observation) and booted macOS. Entry state, relocation and the watchdog reset work (R-6, R-12).

### L2
- `--rung L2 --go --args wel2.hold=1`; exit 0; handed over 09:42:11; layer at 0x812000000.
- Photo `L2-2-final`: band1 "WATARI EL2  RUNG L2 STEP 2" | band2 "CHECKPOINT 5 OK HOLD: TAKE PHOTO" (green) | band3 "GUEST PASS 0 FAIL 0 SKIP 0" | error "LAST ERROR: NONE" | squares: 0-5 green | last log lines: "CHECKPOINT 0 RELOCATED", "1 STAGE1_ON", "2 BANNER", "CORES (REDISTRIBUTORS) = 0X...01", "3 DISCOVER", "CPU IMPLEMENTS POINTER AUTHENTICATION: HCR_EL2.API|APK SET, THE GUEST OWNS THE KEYS", "HCR_EL2.TID3 SET: ID REGISTERS EMULATED, ID_AA64PFR0_EL1.GIC PRESENTED AS 1", "4 IRQ_READY", "AIC AT 0X23B100000", "INPUTS = 0X380", "5 CPU_INIT", "WEL2.STEP=2: PLATFORM AND INTERRUPT CONTROLLER ARE UP, RESETTING THE MACHINE", "HOLD: TAKE THE PHOTO, THEN HOLD THE POWER BUTTON".
- **PASS.** The panel stays usable after m1n1's `fb_shutdown(false)` and the layer's framebuffer mapping works (Q-5, R-2).

### L3-trap
- `--rung L3 --go --args wel2.hold=1`; exit 0; handed over 09:49:12; layer 0x810000000, guest 0x812000000; arguments `wel2.step=3 wel2.cpuif=trap wel2.guest=0x812000000 wel2.hold=1`.
- Photo `L3-trap-2`: band1 "WATARI EL2  RUNG L3 STEP 3" | band2 "CHECKPOINT 28 OK (GUEST CHECK 12) HOLD: TAKE PHOTO" (green) | band3 "GUEST PASS 8 FAIL 0 SKIP 0" | error "LAST ERROR: NONE" | squares: 0-8, 17-23, 28 green | last log lines: "TRAPPED ICC_EOIR1/DIR WRITES = 0x1", "OTHER TRAPPED ICC ACCESSES = 0xE", "EOIR/DIR WITHOUT A MATCHING ACTIVE LR = 0", "TID3 ID-REGISTER READS EMULATED = 0x2E", "M1 FIQS = 0", "GUEST VIRTUAL TIMER (PPI 27) = 0", "GUEST PHYSICAL TIMER (PPI 30) = 0", "FAST IPIS = 0", "VGIC MAINTENANCE = 0", "MAINTENANCE QUENCHED BY THE BACKSTOP = 0", "PMU / UNCORE PMU = 0", "SPURIOUS = 0", "STORMS = 0", "PSCI SYSTEM_RESET -> WATCHDOG (NO FIRMWARE)", "HOLD: ...".
- **PASS.** The per-check lines of G1-G6 had scrolled out (log area full); the band and squares carry the result.
- Difference from the procedure text: it expected `PASS 7` and square 28 amber (`RUNNING (GUEST CHECK 11)`); with `wel2.hold=1` the layer reports the final check as `CHECKPOINT 28 OK (GUEST CHECK 12)` and counts it, so `PASS 8`. Not a failure; the procedure's expected lines should be updated.

### L3-trap-notc
- `--rung L3 --go --cpuif trap-notc --args wel2.hold=1`; exit 0; handed over 09:54:09.
- Photo `L3-notc-1-final`: band1 "WATARI EL2  RUNG L3 STEP 3" | band2 "CHECKPOINT 28 OK (GUEST CHECK 12) HOLD: TAKE PHOTO" (green) | band3 "GUEST PASS 8 FAIL 0 SKIP 0" | error "LAST ERROR: NONE" | squares: 0-8, 17-23, 28 green | last log lines: "TRAPPED ICC_EOIR1/DIR WRITES = 0x1", "OTHER TRAPPED ICC ACCESSES = 0x6", the other counters 0x2E (TID3) and 0 as in `trap`, "PSCI SYSTEM_RESET -> WATCHDOG (NO FIRMWARE)", "HOLD: ...".
- **PASS.** Other trapped ICC accesses 0xE under `trap` vs 0x6 under `trap-notc`: the 8 accesses of the `TC` group (PMR/CTLR/RPR in G5b) trap only when `TC` is set, as designed (R-1b).

### L4
- `--rung L4 --go --args wel2.hold=1`; exit 0; handed over 09:59:20 (after one boot of macOS, F-29 habit).
- Photo `L4-1`: band1 "WATARI EL2  RUNG L4 STEP 4" | band2 "CHECKPOINT 28 OK (GUEST CHECK 12) HOLD: TAKE PHOTO" (green) | band3 "GUEST PASS 10 FAIL 0 SKIP 0" | error "LAST ERROR: NONE" | squares: 0-8, 17-25, 28 green, none grey | last log lines: "PMU / UNCORE PMU = 0", "SPURIOUS = 0", "STORMS = 0", "PSCI SYSTEM_RESET -> WATCHDOG (NO FIRMWARE)", "HOLD: ...".
- **PASS:** G7 (virtual timer, INTID 27) **and G8 (EL1 physical timer, INTID 30)** pass; the procedure expected G8 SKIP ("P gate not implemented"): the gate is evidently in the bundle's code. Tick counts scrolled out (in the video, not transcribed).

### L5
- `--rung L5 --go --args wel2.hold=1`; secondaries started by the host (Flavio Ciani and Marco both agreed in chat); exit 0; handed over 10:02:45.
- Photo `L5-1`: band1 "WATARI EL2  RUNG L5 STEP 5" | band2 "CHECKPOINT 28 OK (GUEST CHECK 12) HOLD: TAKE PHOTO" (green) | band3 "GUEST PASS 12 FAIL 0 SKIP 0" | error "LAST ERROR: NONE" | squares: 0-10 green (9 CPU_ACCEPT, 10 CPU_ARRIVE), 17-28 green | last log lines: "HOLD: TAKE THE PHOTO, THEN HOLD THE POWER BUTTON" (the log area had filled and restarted).
- **PASS:** G9 and G10 green, all 12 guest checks pass. The per-core MPIDR lines were not captured (they scrolled out; the run is too fast to photograph during G9): "all 7 secondaries reported" is **[hypothesis]** inferred from the G9 PASS.

## Hypotheses settled / still open
| Item | Result |
|---|---|
| Q-3 `/chosen/framebuffer` filled from the minimal DTB | yes (0x9e2570000, 2560x1600) |
| Q-5 panel after m1n1 `fb_shutdown`, format, stride | stays on and readable; x2r10g10b10, stride 10240 |
| R-1a `TALL0/TALL1/TDIR` on the M1 | works (both configurations) |
| R-1b `TC` | works (0xE vs 0x6 trapped accesses) |
| R-2 blind-run risk (screen readable) | readable at every rung |
| R-4 FIQ timers to vPPI | G7 and G8 pass |
| R-5, R-15 (maintenance interrupt, SError) | not observed (maintenance 0, no exception) |
| R-6 position independence (L1) | ok |
| R-7 cache incoherence (L1-L2, L5) | not observed |
| R-9 proxy ABI (L0, L1) | ok with m1n1 v1.6.1 on the target and the 2026-11-13 proxyclient |
| R-10 guest RAM bookkeeping | ok |
| R-11 / Q-7 windows 0xF_0000_0000, 0xF_1000_0000 free | nothing in the fdt dump there |
| R-12 watchdog reset | ok (L1 twice, under 5 s) |
| R-13 panic counter / forced power-offs | none observed; macOS booted once mid-day |
| Q-2 (day-1 hang with an armed LR) | not reproduced: G6 delivers, acks and EOIs an SGI through the layer |
| G3 PAuth / TID3 | `API/APK` set, 0x2E ID-register reads emulated, G3 PASS |

## New failures and decisions
- F-32 (`docs/FAILURES.md`): the proxyclient of 2026-11-13 runs `brew --prefix llvm` at import; the expected lines for `hold=1` (`PASS 8`, square 28 green) differ from the procedure text; MPIDR/tick lines scroll out before the final photo.
- ADR candidate: keep `wel2.cpuif=trap` as the M1 default (both work). Next step: T11 (EDK2 on top of the layer on the M1).

## Deferred / not done
- Transcription of the videos (tick counts of G7/G8, MPIDRs of G9): not done; the videos stay on the host and are deleted at cleanup unless Marco wants them.
- DFU dry run: not offered again today (still unproven, F-29).

## Observation added after the run (Marco, 2026-11-13)
- **Panel brightness carries over:** the backlight level set in macOS stays in effect after rebooting into m1n1 and the layer (neither drives the backlight). With low brightness in macOS the M1 screen was dim under m1n1; raising it to the maximum in macOS before the reboot kept it at maximum. [verified: Marco's observation, day 2] -> preparation step for every hardware day: set brightness to maximum in macOS before booting into m1n1 (better photos).
