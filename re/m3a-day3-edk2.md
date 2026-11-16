<!-- SPDX-License-Identifier: MIT -->
# M3a day 3: EDK2 on top of the Watari EL2 layer on the real M1 (j293)

- **Date:** 2026-11-16 (the "Monday" run, `docs/procedures/m3a-monday-runbook.md`). **Who:** Marco (target) + Ciani (host), guided by Claude on the host, procedure `docs/procedures/m3a-day3-hardware.md`, driven with `tools/m3a/monday.py`.
- **Target:** MacBook Pro 13" M1 (j293 / t8103), 8 GB (`mem_size_act 0x200000000` in the m1n1 console); OS firmware 13.5 (iBoot-8422.141.2); m1n1 v1.6.1 on the tethered volume.
- **Host:** Ciani's Mac, USB-C cable to the target's left port nearest the hinge; iPhone for photo and 1080p video.
- **Commit** of the bundle: `84f37c4` (clean tree); `SHA256SUMS` of the day-3 bundle `97b3a199…`, 52 files OK; `WATARI_M1_EFI.fd` `aa6a2eea…` (manifest: model j293, bench 0, bgr8 0); EDK2 "built at" 22:29:53 on Nov 15 2026 (read at D2).
- **Proxyclient:** m1n1 `main` source archive of 2026-11-16, commit `f41fa7b`, sha256 `d4934528…`; the only commit since 2026-11-12 touches the CI workflows, so the proxyclient equals day 2's.
- **Preconditions:** day 2 all PASS: yes; serial kit: not used; Marco's Time Machine backup confirmed on the day: yes. Rung E ran first, before D0.

## Verdict
- **EDK2 boots to BDS, GOP, self-test PASS, reset path (band mode): YES** (D2, D3, D4 PASS).
- Configuration that passed: **both** `trap` (D2/D3, one core) and `trap-notc` with the seven secondaries started by the host (D4).
- Full-panel mode (`wel2.fb=guest`, D5): not run (not needed for Windows band mode).

## Per rung

### E (ehw2, read-only)
- `python3 ehw2.py --out …/ehw2-out.txt`; exit 0; 741 reads, 37 faulted (all `dart-apcie0`: PCIe not powered while m1n1 serves the proxy).
- Coprocessors: ANS `CPU_CONTROL 0` (stopped), NVMe `BOOT_STATUS 0` (not up); SEP stopped; DCP running (`CPU_CONTROL 0x10`).
- SART: entries 0-2 in MMIO/SRAM (`0x23d2b8000`, `0x23e414000`, `0x23e434000`); entry 3 `0x9ffe20000..0x9ffe54000`; entry 5 `0x800034000..0x8001d4000`. DART TTBRs: dart-disp0 `0x9fff34000`, `0x9fff04000`; dart-dcp `0x9fff0c000` (locked=1; dart-disp0 locked=0); dart-usb0/1 `0x806f7c000`, `0x807458000` (m1n1's own USB).
- Compared with D0's guest `/memory` (`0x808000000..0x810000000`, `0x812000000..0x9de000000`) and every block of the plan: **no overlap** (H4 answered for this state). Verdict PASS. The automatic FAIL was the log-hygiene check matching property *names* (F-40).
- Caveat: the state while m1n1 serves the proxy, not the hand-over state.

### D0 (no jump)
- `--rung D0 --edk2 WATARI_M1_EFI.fd --fb band --no-smp`; loader exit 0, about 1 minute.
- Blocks: layer `0x810000000`, guest spacer `0x812000000`, edk2 `0x814000000`; BLOCK CHECK and GUEST FDT CHECK PASS.
- Guest FDT: `/memory` `0x812000000 + 0x1cc000000` (7360 MiB, holds the EDK2 block) and `0x808000000 + 0x8000000`; image at `0x814000000`, guest FDT at `0x814300000`; framebuffer plan band 360 / 1240 rows, guest base `0x9e28f4000`; panel 2560x1600, stride 10240 (= 4 x width), x2r10g10b10; one `cpu@0`; PSCI `hvc`; GIC reg `<0xf 0x0 0x0 0x10000 0xf 0x10000000 0x0 0x20000>` (one redistributor frame for one core; the check's 0x100000 expectation is F-40).
- Verdict PASS.

### D1 (L5 + `wel2.fb=band`)
- T0 (`kboot_boot`) 10:00:05; boot arguments `wel2.step=5 wel2.cpuif=trap wel2.guest=0x812000000 wel2.fb=band wel2.hold=1`.
- Photo D1-1 (T0+9 s): band 1 `WATARI EL2  RUNG L5 STEP 5`; band 2 `CHECKPOINT 28 OK (GUEST CHECK 12) HOLD: TAKE PHOTO` (green); band 3 `GUEST PASS 13 FAIL 0 SKIP 0`; `LAST ERROR: NONE`; ticker `WATARI EL2: HOLD: TAKE THE PHOTO, THEN HOLD THE POWER BUTTON` (the panel font is upper case). Squares 0-10 and 17-29 green. Four bars red, green, blue, yellow below the band; nothing in the band.
- Verdict PASS.

### D2 + D3 (`trap`, one core, `--pmlog`)
- T0 10:03:23; boot arguments `wel2.cpuif=trap wel2.guest=0x814000000 wel2.pmlog=0x812000000,0x200000 wel2.abi=fdt wel2.gfdt=build wel2.fb=band wel2.hold=1`.
- Timings from video frames: `UEFI FIRMWARE (VERSION  BUILT AT 22:29:53 ON NOV 15 2026)` at T0+0.5 s; DXE lines T0+1..4 s; `SELFTEST ROUNDTRIP OK: WROTE R80 G40 B20, READ R80 G40 B20 (SUCCESS)` at T0+4.2 s; `SELFTEST TICK 1/10` T0+5.2 s … `9/10` T0+13 s; `WATARI EL2: STATS: …` lines, then HOLD at T0+14.5 s.
- Final photo D3-2: band 1 `WATARI EL2  RUNG - STEP -`, band 2 `CHECKPOINT 8 OK HOLD: TAKE PHOTO` (green), band 3 `GUEST PASS 0 FAIL 0 SKIP 0`, `LAST ERROR: NONE`; four bars red, green, blue, white; ten white boxes.
- The GOP line, `SELFTEST PASS` and the reset line were replaced within one video frame (30 fps) and not caught; HOLD after the reset path implies them.
- Verdicts D2 PASS, D3 PASS.

### PM0
- Power-off ~10 s, tethered boot again at once; `monday.py postmortem`: **NOLOG** (no header at `0x812000000`). The M1's DRAM does not keep the ring across a power-off; photos and video only from then on.

### D4-0 and D4 (`trap-notc`, `--smp`)
- D4-0 preview: 8 `cpu@` nodes (`cpu@0..3`, `cpu@10100..10103`), GIC reg `… 0x0 0x100000>`, all checks PASS.
- D4: T0 10:09:47; boot arguments `wel2.cpuif=trap-notc wel2.guest=0x814000000 wel2.pmlog=0x812000000,0x200000 …`. Photo D4-1 (T0+17 s): identical to D3-2; **exactly squares 0-8 green, no 9/10** (EDK2 made no `CPU_ON`). Verdict PASS.

## Hypotheses settled / still open
| Item | Result |
|---|---|
| Q-E2 stride = 4 x width | yes (10240 for 2560) |
| R-E3 band arithmetic (360 / 1240 rows) | ok (D1, D3) |
| R-E2 framebuffer mapping | ok (D1 bars, D3 picture and Blt round trip) |
| R-E4 PrePi with the real memory map (EDK2 above 4 GB) | ok |
| R-E5 GICv3 selection | ok (DXE ran on to BDS) |
| R-E6 EDK2's timer tick | ok, one per second |
| R-E10 cost of the debug log | not a problem: BDS self-test in ~4 s from the jump |
| R-E11 hypercall console | ok |
| Procedure time estimates | far too long: D0 ~1 min, D2 to HOLD ~15 s |
| PM0: DRAM keeps the ring across a power-off | **no** (NOLOG) |
| ehw2 verdicts | no coprocessor buffer in guest RAM in the proxy state (above) |

## New failures and decisions
- F-40: three false FAILs of `monday.py` checks (E log hygiene, GIC node with one core, `RAMDISK written` regex).
- F-41: forced power-offs counted wrongly; the M1 came up in the Startup Recovery Assistant.

## Deferred / not done
- D3b, D5, D5 BGR8: optional, not run (Windows used band mode).
