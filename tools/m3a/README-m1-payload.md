# Watari EL2 hardware-day bundle (M3a, first boot of the layer on the M1)

Our writes on the target are RAM only: images, the DTB and m1n1's FDT copy go into m1n1's heap through the USB proxy.
m1n1's own `kboot_prepare_dt` / `kboot_boot` also do non-persistent hardware set-up (ISP heap, display DART lock, USB/PCIe
bring-up, SEP random bytes); a power cycle resets all of it **[hypothesis: not every helper was read]**. We touch no NVMe,
NVRAM, PMU or boot policy.
Claims are marked **[verified: source]** or **[hypothesis]**; everything not yet run on the hardware is **[check]**.

## What is in the bundle

| File | What |
|---|---|
| `wel2-m1.bin` | the layer, M1 build with the built-in EL1 payload (rungs L1, L2) |
| `wel2-chain.bin` | the layer, chain build: enters the guest named by `wel2.guest` at EL1 (rungs L3-L5) |
| `m1guest.bin` | EL1 test guest, checks G1-G12 (rungs L3-L5) |
| `j293-minimal.dts/.dtb` | minimal j293 device tree that m1n1's `kboot_prepare_dt` fills (DESIGN 4.2) |
| `watari-load.py` | the host loader |
| `ehw2.py` | optional read-only inventory of the state inherited from iBoot/m1n1 (`docs/procedures/m3a-ehw2-rung.md`) |
| `monday.py` | the Monday runner for the Claude session on the host Mac (T16i): `preflight`, `rung <ID>` (the procedures' exact loader commands, automatic pre-jump checks, photo checklist, results file), `next`, `verdict`, `confirm`; see `docs/procedures/m3a-monday-runbook.md` |
| `WATARI_M1_EFI.fd` | EDK2 for the M1 (`tools/build-edk2-m1.sh`, copied from `$WATARI_DATA/build-m1/`), the guest of rungs D0 and D2 (day 3) |
| `WATARI_M1_EFI.fd.build.txt` | the firmware's build manifest (written by `tools/build-edk2-m1.sh`: model, bench, ramdisk stand-in, bgr8, tag, PCD, commit, SHA-256); `build-m1-payload.sh` refuses a bench, tagged, non-j293 or manifest-less firmware (`WATARI_M1_EFI_ALLOW_BENCH=1` for a bench bundle) |
| `fdt_m1_dump` | prebuilt arm64 host tool (the layer's FDT parser), covered by `SHA256SUMS`; no compiler needed on the host |
| `gfdt_dump` | prebuilt arm64 host tool (day 3): the layer's own guest-FDT builder (`el2/vgic/fdt_build.c`, E5) compiled for the host; from m1n1's prepared FDT it prints the guest FDT the layer would build (see "Day 3" below) |
| `fdt-dump-src/` | the sources of both tools plus `build.sh`; the loader builds from it only if `xcode-select -p` succeeds, never otherwise (the `cc` shim can open the Command Line Tools installer) |
| `winram/<image>`, `.sha256`, `.manifest.txt` | **only if built with `--winram`** (day 4, rung W1): the Windows RAM-disk image of `tools/m3a/build-winram-image.sh` and its sidecar. **Not in `SHA256SUMS`** (about 700 MiB): the loader hashes the image itself and compares it with the sidecar before any proxy call. May also be copied to the host separately (any path works for `--ramdisk`) |
| `SHA256SUMS` | the loader refuses to run if any file differs (the `winram/` folder is outside it) |

Built on Marco's Mac by `tools/m3a/build-m1-payload.sh [DIR]` (default `$WATARI_DATA/m3a-payload`, never inside the
repository, no network, nothing downloaded: Homebrew LLVM, `dtc` and the host `cc` already on the Mac). `WATARI_M1_EFI=<file>`
picks another EDK2 image, `WATARI_NO_EDK2=1` builds a day-2 bundle without one. **BGR8 variant (H7, optional):** `tools/build-edk2-m1.sh --bgr8` builds `WATARI_M1_EFI-bgr8.fd`; it is not in the default bundle: build a second bundle with `WATARI_M1_EFI=<that file> build-m1-payload.sh <other DIR>` (the bundle lands in `<DIR>/bundle/`). **Windows image (T23b):** `build-m1-payload.sh --winram default <DIR>` (or `--winram <file>`; `default` = `$WATARI_DATA/winram/winram.img`) also copies the image and its sidecar into `<DIR>/bundle/winram/` and re-checks the copy against the sidecar; an image from inside the repository is refused.

## On the host Mac (zero footprint)

Nothing is installed. The bundle lives in a temporary folder inside `~/WatariBoot-host/` (the day-1 folder, see
`docs/procedures/m3a-day1-hardware.md` section 0) and is removed afterwards with `rm -rf`. The loader needs only the day-1
m1n1 checkout (`--m1n1 <checkout or its proxyclient>` or env `M1N1_DIR`) and its venv Python (pyserial, construct).
It imports `m1n1.proxy`, `m1n1.proxyutils`, `m1n1.sysreg` and **never `m1n1.setup`** (it writes the PMU, F-29).
Do not use `python3 -I`: it would ignore `PYTHONPATH`; the loader sets its own path.

```
source ~/WatariBoot-host/env.sh
export M1N1DEVICE=$(ls /dev/cu.usbmodem*1)         # the proxy port, never copy the serial number into notes
cd <bundle folder>
python3 watari-load.py --rung L0 --dry-run          # no device: prints every proxy call, checks the bundle
python3 watari-load.py --rung L0 --m1n1 ~/WatariBoot-host/work/m1n1-main
python3 watari-load.py --rung L1 --go --m1n1 ...    # first jump
```

Rungs L1-L5 jump and therefore need `--go`. A reboot back into m1n1 tethered mode is needed between jumping rungs.

## Rungs (DESIGN 9)

| Rung | Command | What it does | Expect |
|---|---|---|---|
| L0 | `--rung L0` | writes the DTB, sets no wel2 arguments, `cpufreq_init` + `smp_start_secondaries` (volatile; without alive secondaries m1n1 removes their cpu nodes and the FDT would show 1 cpu) **[check: cpufreq_init needed]**, `kboot_prepare_dt`, reads the prepared FDT back, saves it in `out/`, runs `fdt_m1_dump` on it, and memaligns (without writing) the two 32 MB blocks the jump rungs use, then checks on the host that each lies wholly inside one *usable* range (FDT RAM minus reservations): `BLOCK CHECK: PASS`/`FAIL` (exit 4 on FAIL: do not run L3-L5, the layer would refuse the guest; this tests whether m1n1's proxy heap is plain usable RAM, **[check]**). **No jump.** | `kboot_prepare_dt OK`, then `platform m1 (apple,aic found)`, 8 cpus with `release_addr` set for the secondaries, memory/usable/reserved ranges, aic, uart, wdt, framebuffer 2560x1600. Proxy still alive. |
| L1 | `--rung L1 --go` | `wel2-m1.bin`, `wel2.step=1`: hand-over, the layer masks DAIF, relocates itself, writes the watchdog reset | the **Mac restarts within about a second**. No display needed. |
| L2 | `--rung L2 --go` | `wel2-m1.bin`, `wel2.step=2`, `wel2.cpuif=trap`: MMU, FDT parse, UART, AIC/FIQ init, banner, then watchdog reset | squares 0.. green and the status band on the panel, then a restart |
| L3 | `--rung L3 --go [--cpuif trap-notc]` | `wel2-chain.bin` + `m1guest.bin`, `wel2.step=3 wel2.cpuif=<..> wel2.guest=0x<guest block>`: stage 2, guest G1-G6 on one core. Run once with `trap`, once with `trap-notc` | band shows `GUEST PASS n FAIL 0`; reset at the end |
| L4 | `--rung L4 --go` | L3 plus timers G7-G8 | as L3 |
| L5 | `--rung L5 --go` | L4 plus the secondaries started by the host (`smp_start_secondaries`), G9-G10 | as L4 |

### Day 3: EDK2 on the layer (PLAN T13 H2-H8)

New loader options: `--edk2 <file of the bundle>` (rungs D0 and D2; the file must be in `SHA256SUMS`, so it is verified before any
proxy call), `--fb layer|band|guest` (default `layer`, passed through as `wel2.fb=<mode>`; the loader does not know which
modes the layer implements, an unknown one is the layer's to refuse), `--gfdt-dump <tool>`, `--smp` (D4: the host starts the other cores).

| Rung | Command | What it does | Expect |
|---|---|---|---|
| D0 | `--rung D0 --edk2 WATARI_M1_EFI.fd --fb band` | like L0 (secondaries, `kboot_prepare_dt`, read-back, `fdt_m1_dump`) with **three** unwritten 32 MB blocks (layer, test-guest spacer printed as `BLOCK  guest`, EDK2): `BLOCK CHECK` over all three; then `gfdt_dump --layer <layer block> --entry <EDK2 block> --image-size <EDK2 header> --fb <mode>`: prints the guest FDT, `dtc`'s view of it when `dtc` is installed, and `GUEST FDT CHECK: PASS` (the layer's own placement rule: the first `/memory` range holds the image, the FDT and PrePi's 64 MB region and is at least 128 MB). **No jump; none of our code runs on the target** (m1n1 still does its own prepare steps, and D0 starts the secondaries like L0, which is volatile). **D0 starts the secondaries, D2 does not**: the FDT D0 previews then has 8 cpus, the one D2 prepares has 1 unless `--smp`. Run D0 with `--no-smp` unless D4 (`--smp`) is planned, so that it previews what D2 will see | the `/memory` nodes (first one contains the EDK2 block), GIC at `0xF00000000`/`0xF10000000`, framebuffer geometry and whether stride = 4 x width (`// framebuffer of the prepared FDT:` line), no Apple node or identifying property. Exit 4 = block check or placement refused: do not run D2 |
| D2 | `--rung D2 --edk2 WATARI_M1_EFI.fd --fb band --go [--cpuif trap-notc] [--smp]` | the chain layer plus EDK2 in its own block, arguments `wel2.cpuif=<..> wel2.guest=0x<EDK2 entry> wel2.abi=fdt wel2.gfdt=build wel2.fb=<mode>` (no `wel2.step`), the same read-back and checks as D0 on the real addresses (a stop on a refusal; if the FDT address cannot be captured or the header is wrong it does **not** jump: exit 3, rerun with `--fdt-addr 0x<address of the m1n1 console line 'FDT prepared at ...'>`; exit 1 if `gfdt_dump` or `fdt_m1_dump` fails for any reason other than a placement refusal (exit 1 of `gfdt_dump`, mapped to exit 4)), then the jump | H4-H7 of the PLAN; D3 is the same run continuing, D4 adds `--cpuif`/`--smp`, D5 is `--fb guest` |

D1 (test guest with check G12 and `wel2.fb=band`) is `--rung L5 --fb band` (the test guest, `wel2.fb` is appended when it is not `layer`) **[check:
that L5's check mask includes G12]**. D2 allocates the same three blocks as D0 (the second is an unwritten spacer), so the addresses D0 checked are
the ones D2 uses **[hypothesis: m1n1's heap allocates the same way twice; the read-back of D2 checks it again]**.

`gfdt_dump prepared.dtb [--layer A] [--entry E] [--image-size N] [--fb layer|band|guest] [--gran 4k|16k] [-o guest.dtb]` runs the layer's own M1 parser
(`fdt_m1_parse`) and the guest-FDT builder on the prepared FDT, so only RAM, reservations, cpu `reg` values and framebuffer geometry reach
the output; the builder refuses the identifying property names, and the tool's own scan exits 3 on any node, property or string with
`apple` in it. It also prints on stderr what it cannot know (`NOTE:` lines): without `--layer` the layer's block is not cut out of the RAM; without `--entry` the placement rules are not checked; the MSI frame is listed when `0xFFFFF000` is not guest RAM (the layer's `g_v2m_on` **[hypothesis: same rule]**). The framebuffer node is what the layer builds: `wel2_fb_plan` of `el2/vgic/fb.c` with the 16 KB granule (`--gran 4k` for `wel2.gran=4k`; the loader adds it by itself when `wel2.gran=4k` or `wel2.gran=16k` is in `--args`, 16k winning as in the layer's `stage1.c`): for `band` the node starts below the band (`// framebuffer plan:` line with band rows, guest rows, guest base; 360 and 1240 rows for 2560 x 1600, stride 10240 **[verified: test_gfdt_dump.py, same function as the layer]**), and when the plan fails (no framebuffer, not whole pages, no room) the output says so and has no node, as the layer then keeps the panel. The self-check also scans the raw bytes of every property value for `apple`, any case. Tests: `python3 -I tools/m3a/test_gfdt_dump.py` (j293 fixture of T6b, which carries a serial number and a
MAC address on purpose).

### Windows rung W1: the RAM-disk image (watari-winram DESIGN 2, PLAN T23)

Needs the day-3 gate first (D3/D4 pass) and Marco's backup confirmation (`docs/design/watari-winram/PLAN.md`, T24). The image is
a raw GPT/FAT32 disk made by `tools/m3a/build-winram-image.sh` on Marco's Mac (`<image>` + `<image>.sha256`); it holds Microsoft files, so
it lives only under `$WATARI_DATA/winram/` and in the bundle folder, never in the repository.

| Option | Meaning |
|---|---|
| `--ramdisk <image>` | with `--edk2` on D0 or D2 (other rungs: refused, exit 2). A path, or a name relative to the bundle folder (`winram/winram.img`). A **fourth block** |
| `--pmlog` [`--pmlog-size <KiB>`, `--pmlog-base 0x..`] | T16j, any rung: the **post-mortem log**. Allocates the ring (an own 32 MB block, LAST, so the other blocks keep their addresses; or the fixed `--pmlog-base`, 16 KB aligned), zeroes `--pmlog-size` KiB (default 1024), passes `wel2.pmlog=0x<base>,0x<size>`, writes `<out>/pmlog-base.json` at the jump (not in `--dry-run`); BLOCK CHECK covers the region |
| `--postmortem` [`--pmlog-base 0x..`] | after the Mac is back in m1n1 proxy mode (the reset, then the m1n1 boot): reads the ring back (**reads only**), validates magic and CRC, writes `<out>/pmlog.txt` and `pmlog.bin`, prints the header and the last 30 lines. Exit 0 ok, 5 no valid header (DRAM did not keep it or wrong base), 6 CRC bad / inconsistent (text kept, UNVERIFIED). **Whether DRAM survives the reset is unknown [hypothesis]: a Monday check** |
| `--ramdisk-test <MiB>` | the **rate test**, no `--rung`: one 32 MB aligned block, `<MiB>` of a test pattern written in slices, read back and compared, MB/s each way, an estimate for a 704 MiB image. No DTB, no layer, no boot arguments, no jump, no write outside its block |
| `--ramdisk-slice <MiB>` | slice size (default 16) |
| `--ramdisk-verify sample\|full\|none` | read-back per slice: first and last 4 KiB (default), the whole slice, or none |
| `--no-data-csums` | require m1n1's `Feature.DISABLE_DATA_CSUMS` for the transfer. **[verified: m1n1 v1.6.1 proxy.py]** the proxyclient already offers it in every handshake (`nop()`), so it is on whenever the firmware agrees and cannot be switched on from here: the flag only checks `iface.enabled_features` and refuses (exit 2, before any write) if the feature is off. With it on, a corrupted slice is caught only by the read-back (`--ramdisk-verify`) and the trailer CRC in EDK2 |

What `--ramdisk` does, in order (asserted by `test_watari_load.py`):
1. **Before any proxy call:** the image is hashed (SHA-256, CRC-32) and compared with `<image>.sha256` (missing sidecar or a difference: exit 2); the size limit is checked: the block (image + 4 KiB trailer, rounded to 1 MiB) must be at most **896 MiB**, so that the 32 MiB alignment skip, the image block rounded to 32 MiB and the three other 32 MiB blocks stay within the proxy heap's 1 GiB (exit 2). The **plan** is printed: size, hashes, block size, slices, heap need, and the transfer time at 1 / 3 / 10 MB/s **[hypothesis: unmeasured; run `--ramdisk-test` first]**.
2. DTB written, then the image block is allocated **first** (`memalign(32 MB, size)`, first-fit from the low end of the heap, so the range above stays one large `/memory` piece for EDK2), then written in 16 MiB slices: one `writemem` each (its own m1n1 checksum), a checksum error retries that slice once and then aborts, read-back per `--ramdisk-verify`, one progress line per slice (MiB, MB/s, ETA). Then the **4 KiB trailer, last, at the END of the range** (`base + size - 4096`): `WRAMDSK1`, `image_bytes` u64, CRC-32 u32 (`zlib.crc32`, **[hypothesis: the polynomial of UEFI `CalculateCrc32`; settled by T21b's test]**), SHA-256 (32 bytes), zero padding.
3. Layer, spacer and EDK2 blocks as before; cache maintenance (`dc_cvau`/`dc_cvac`/`ic_ivau`) over the whole ramdisk range and the other images; **then** the boot arguments, with `wel2.ramdisk=0x<base>,0x<size>` right after `wel2.guest=` (`size` includes the trailer). The layer needs `wel2.abi=fdt` for it (set with `--edk2`).
4. D0/D2 read the prepared FDT back; **`BLOCK CHECK`** covers the **whole** ramdisk range (not just 32 MB): inside one usable range, 16 KB aligned, no overlap with the layer, spacer, EDK2 blocks, the prepared FDT or the framebuffer (exit 4 on FAIL). Then `gfdt_dump ... --ramdisk 0x<base>,0x<size>` previews the carve (the loader looks for the option string in the tool; if this `gfdt_dump` has no `--ramdisk` yet (T21a adds it) it says so and skips that part: the layer's own FATAL line is then the backstop).
5. **D0 writes the image too** (it is RAM, and the preview reads samples back); D2 writes it again after a new m1n1 session. Use `--ramdisk-test` for the rate, D0 for the placement, D2 for the jump.

```
python3 watari-load.py --ramdisk-test 64 --m1n1 ...                                   # the rate test (also --dry-run)
python3 watari-load.py --rung D0 --edk2 WATARI_M1_EFI.fd --ramdisk winram/winram.img --no-smp --m1n1 ...
python3 watari-load.py --rung D2 --edk2 WATARI_M1_EFI.fd --ramdisk winram/winram.img --fb band --go --m1n1 ...
```

Dry run on the real image (`winram.img`, 747 634 688 bytes = 713.0 MiB): block 714 MiB, 45 slices, heap need 864 MiB of 1024; the dry run keeps nothing in host memory and does not compare read-backs.

L1-L4 start no secondaries: m1n1 then removes the 7 other cpu nodes, so their FDT has 1 cpu and the layer accepts it (it runs on the boot core only). Only L0 and L5 show/use all 8.

Layer and guest are each placed at the start of their own 32 MB-aligned, 32 MB block from m1n1's heap (D-2). The hand-over
(`wel2.guest` entry, guest RAM visible to the layer) for the M1 is T10/T11 work: **[hypothesis]** that L3-L5 reach
the guest on the first try; the layer says so itself with a FATAL line if the info block is not in guest RAM.

`--dry-run` works for every rung and opens no device. `--args "wel2.xxx=..."` appends boot arguments. `--no-smp` / `--smp`
override the secondary start. `--boot-timeout` (default 40 s, as `linux.py`) is how long the loader waits for a reply that may never come.

### Call order (asserted by `test_watari_load.py`; D2: DTB, then the layer block, the spacer and the EDK2 block, with `wel2.guest` = the EDK2 block; D0/D2 add the read-back and `gfdt_dump` after `kboot_prepare_dt`)

verify sums -> `malloc` DTB + write -> `memalign` 32 MB (layer, then guest) + write -> `dc_cvau`/`dc_cvac`/`ic_ivau` over each image (the real guarantee is m1n1's set/way flush in `mmu_shutdown`)
-> **then** `kboot_set_chosen("bootargs", ...)` (the arguments contain the guest address, DESIGN L-2) -> `cpufreq_init` + `smp_start_secondaries`
(L0, L5) -> `kboot_prepare_dt` -> L0 stops (read back FDT, `fdt_m1_dump`) -> DAIF = 0xc0 -> `kboot_boot`. A timeout or
transport error after the request left the host counts as "handed over"; one before it does not.
(`kboot_set_chosen` must precede `kboot_prepare_dt`: the chosen parameters are applied inside `dt_set_chosen`
[verified: m1n1 v1.6.1 `src/kboot.c`].)

## What to expect on the screen

Read the panel from a photo as described in `el2/boot/README.md`, "Reading the screen from a photo": squares row at the top
(green ok, amber running, red fail), a three-line status band (`WATARI EL2  RUNG L<n> STEP <s>`, `CHECKPOINT <n> ...`,
`GUEST PASS <p> FAIL <f> SKIP <k>`), the `LAST ERROR:` line (copy it exactly if not `NONE`), and the scrolling log below.
Photograph at each rung: the log wipes itself when full. A black panel may be display sleep (press a key) or a
framebuffer that m1n1 turned off **[hypothesis]**.

## Abort and recovery

- Abort criteria: L1 no restart within 5 s of the 'handed over' line (the loader itself may wait up to 40 s for a reply that never comes); later rungs 60 s without a new checkpoint, a red square, any `FATAL`/`unexpected` text.
- Hold the power button about 10 s until the screen is dark. A normal press then boots macOS (the default).
  To run again: from off, hold the power button until the startup options appear and pick the m1n1 volume
  (day-1 procedure 3.4), reconnect, wait for `/dev/cu.usbmodem*1`.
- Boot macOS once between hardware sessions (unclean power-offs and the PMU panic counter, F-29).
- If `kboot_prepare_dt` fails (returns non-zero) nothing of ours has run and the proxy is alive: try the fallback DTB.

## Fallback DTB (manual step, never fetched by a script)

If m1n1 rejects `j293-minimal.dtb` at L0, pass an upstream `t8103-j293.dtb` with `--dtb <file>`: built beforehand on
Marco's Mac from a Linux tree he already has (or obtains after the backup confirmation), or taken from an Asahi
installation's ESP **[check: where the installer stores it]**. The loader prints the DTB name and hash it used.

## Privacy

The prepared FDT contains the board serial number, SMBIOS data, MAC addresses and RNG seeds (added by m1n1). It is saved only
as `out/prepared-fdt.dtb` in the bundle folder, together with `out/m1n1-console.log` (the m1n1 console text captured while
waiting for replies). Neither is committed or pasted into chat; `fdt_m1_dump` prints only what the layer uses. The guest FDT that
`gfdt_dump` prints (and saves as `out/guest-fdt.dtb`) holds no identifier by construction (see above), so its text may be pasted
into the session notes; the prepared FDT it was built from may not. The loader
echoes only m1n1 console lines about the FDT address and failures. The folder is deleted at the end of the day.

## Declared fallback firmware (T16g, day 4 only)

`build-m1-payload.sh --fallback-efi e4=<WATARI_M1_EFI-bgr8-e4.fd> --fallback-efi e1=<WATARI_M1_EFI-bgr8-e1.fd>` adds `WATARI_M1_EFI-e4.fd` and `WATARI_M1_EFI-e1.fd` (plus `.build.txt`) to the bundle, both in `SHA256SUMS`; they are manifest-checked (model `j293-e4` / `j293-e1`, BGR8, no bench, clean tree) and never replace `WATARI_M1_EFI.fd`. Select one with `--edk2 WATARI_M1_EFI-e4.fd` (`--smp`) or `--edk2 WATARI_M1_EFI-e1.fd` (`--no-smp`). Only with Marco's OK: `docs/procedures/m3a-day4-windows-ramdisk.md` 6.3.
