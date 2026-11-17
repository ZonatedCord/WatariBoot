<!-- SPDX-License-Identifier: MIT -->
# M3b day 4: Windows from a RAM disk on the real M1 (j293)

- **Date:** 2026-11-16 (same session as day 3, `re/m3a-day3-edk2.md`). **Who:** Marco (target) + Ciani (host), guided by Claude on the host, procedure `docs/procedures/m3a-day4-windows-ramdisk.md`, `tools/m3a/monday.py`.
- **Target:** MacBook Pro 13" M1 (j293 / t8103), **8 GB**; m1n1 v1.6.1.
- **Host:** Ciani's Mac; stopwatch: the `kboot_boot` banner of `monday.py` (T0) and the video clock.
- **Commit** of the bundle: `84f37c4`; day-4 `SHA256SUMS` `6a642a7c…`, 56 files OK; `WATARI_M1_EFI.fd` (BGR8) `a8cf6ee1…`; fallbacks e4 `29f94a2c…`, e1 `06fd85c6…` (manifests checked by `preflight`, not used); `winram.img` 747634688 bytes, sha256 `09e0e9c6…`, crc32 `0xe3eb84c7`.
- **Gate:** day 3 D3 and D4 PASS in the same session; rung E PASS; image on Ciani's Mac pre-approved by Marco (2026-11-15, decision 2) and deleted with verification at the end; Marco's backup confirmed on the day.

## Verdict
- **Furthest stage reached on the hardware: S1** — the RAM disk is registered and BDS starts `\EFI\BOOT\BOOTAA64.EFI` from it; bootmgr then stops at once with a synchronous exception that EDK2's default handler reports (`ASSERT [ARMCPUDXE] CPUEXCEPTIONHANDLERLIB.C(438)`). No Windows logo. Same result with the D4 flags (W2) and with the plain flags (W1c). New failure F-39.
- **Transfer rate:** W1a write 11.38 MB/s, read 27.98 MB/s (with data checksums; `--no-data-csums` not tried); the full image took 1 min 07 s (W2) and 1 min 06 s (W1c); 0 slice retries.
- **Windows on the BGR8 GOP:** not reached.

## Per rung

### W1a (rate test, no jump)
- `--ramdisk-test 64`; block `0x810000000..0x814000000`; four 16 MiB slices at 11.35-11.38 MB/s; `RATE TEST: write 11.38 MB/s, read 27.98 MB/s, read-back identical`; projection about 1 min; decision CONTINUE. PASS.

### W2 (+W3), D4 flags: `trap-notc`, `--smp`
- Fresh m1n1 boot after W1a. RAMDISK block `0x812000000..0x83ea00000` (748683264 bytes), layer `0x840000000`, pmlog `0x842000000`, edk2 `0x844000000`; boot arguments `wel2.cpuif=trap-notc wel2.guest=0x844000000 wel2.ramdisk=0x812000000,0x2ca00000 wel2.pmlog=0x842000000,0x200000 wel2.abi=fdt wel2.gfdt=build wel2.fb=band wel2.hold=1`. `RAMDISK written: 747634688 bytes in 1 min 07 s (11.17 MB/s), trailer at 0x83e9ff000 (crc32 0xe3eb84c7)`. All placement checks PASS (first `/memory` 6590 MiB). T0 10:24:09.
- Ticker from video frames, in order: DXE lines; `WATARIM1RAMDISK: NODE: IMAGE AT 812000000, 2CA00000 BYTES (WITH THE TR…` held ~3 s (the CRC pass; the CRC and `registered` lines were not caught legibly); RamDiskDxe's NFIT line (`… WILL PUBLISH …`); `GRAPHICSCONSOLE … MODE …`; `[BDS] EXPAND \EFI\BOOT\BOOTAA64.EFI -> VIRTUALDISK(0X812000000,0X83E8F…`; a fragment `…BOOTMGFW.EFI`; then the exception dump within ~0.4 s: `PC 0X0009DD8?24?0 (0X0009DD860000+0X0000?4?0) [ 2] …` (name unreadable), `[ 3] …/BUILD-M1-BGR8/…/WATARIM1-AARCH64/DEBUG_CLANG…`, register and stack lines (stack around `0x9DD6557E0`); final, held: `ASSERT [ARMCPUDXE] CPUEXCEPTIONHANDLERLIB.C(438): ((BOOLEAN)(0==1))`.
- Band: `WATARI EL2  RUNG - STEP -`, `CHECKPOINT 8 OK ENTER_GUEST` (green), `GUEST PASS 0 FAIL 0 SKIP 0`, `LAST ERROR: NONE`; squares 0-8 green, no 9/10; panel below the ticker black. Photo W2-1 at T0+42 s.
- Verdict W2 FAIL, W3 not reached (FAIL). Not a core-count symptom: no e4/e1 fallback (table 6.3).

### W1c, plain flags: `trap`, one core (Marco's and Ciani's decision)
- Run after a macOS boot (F-41). RAMDISK `0x810000000..0x83ca00000`, pmlog `0x840000000`, edk2 `0x842000000`; `RAMDISK written: … in 1 min 06 s (11.25 MB/s) … (crc32 0xe3eb84c7)`; T0 10:38:30.
- **Identical stop.** The dump read better this time (digits marked `?` uncertain): **`ESR 0X02000000`**, **`FAR 0X0000000000000000`**; `SYNCHRONOUS EXCEPTION AT 0X00000009?BED00?4…` (rest unreadable); backtrace `PC 0X0009DD6?4CA4 (0X0009DD656000+0X0001E?A4) [ 1] DXECORE.DLL` and `PC 0X0009DD67B744 (0X0009DD656000+0X00025744) [ 1] DXECORE.DLL`; module `[ 2] /TMP/WATARI-DATA/BUILD-M1-BGR8/BUILD/WATARIM1-AARCH64/DEBUG_CLANG…`; X8 `0x256`, X9 `0x2BA`, V6 `0x0040010004040000`. Final ticker as W2; `LAST ERROR: NONE`; squares 0-8.
- Verdict FAIL.

## Hypotheses settled / still open
| Item | Result |
|---|---|
| Q-W3 proxy rate and the 15 min limit | 11.2-11.4 MB/s write, ~1 min per image: not a constraint |
| Q-W4 `--no-data-csums` | not tried |
| R-W1 bootmgr reached from the RAM-disk device path | **yes**: BDS expands `BOOTAA64.EFI` on `VirtualDisk(0x812000000,…)` and starts it |
| R-W2 BCD | not reached |
| R-W4 / Q-W5 BGR8 GOP in band mode | not reached (no picture after GraphicsConsole) |
| R-W5 multi-core kernel | not reached; the stop is the same with one core |
| R-W10 caches / CRC | the RAM disk was registered, which `WatariM1RamDiskDxe` does only after the CRC matches [inferred; the CRC line itself was not caught] |
| R-W15 heat | nothing noticed |
| New: ESR 0x02000000 at EL1 right after bootmgr starts | EC 0x00 = "unknown reason", IL 1 [verified: Arm ARM ESR_ELx]; that it is an instruction UNDEFINED at EL1 on the M1 (and implemented by QEMU, where the bench reaches Setup) is a **hypothesis** for the bench |
| Differences from the bench (T22/T16k) | the bench (QEMU TCG, `qemu-virt-bench` and j293 models) reaches Setup; the M1 stops in bootmgr before any screen |

## New failures and decisions
- **F-39:** bootmgr stops on the M1 with a synchronous exception (ESR 0x02000000) caught by EDK2's handler; independent of `trap`/`trap-notc` and 1/8 cores; the layer reports nothing. Next step: the bench (reproduce the M1's CPU feature set; capture the full exception dump, e.g. with a RELEASE-vs-DEBUG comparison or a dump kept on screen). No analysis of the Microsoft binary.
- F-40 (tooling false FAILs) also hit W2/W1c (`RAMDISK written` regex, GIC node with one core).

## Deferred / not done
- W1b (preview with the image), W1a2 (`--no-data-csums`), W2g (`--fb guest`), the e4/e1 fallbacks: not run (W1b optional; the others need the kernel to start or a core-count symptom).
- A full exception line could not be read: the dump scrolls through the one-line ticker in ~0.4 s and PM0 showed the ring is lost on power-off.
