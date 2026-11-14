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
| `WATARI_M1_EFI.fd` | EDK2 for the M1 (`tools/build-edk2-m1.sh`, copied from `$WATARI_DATA/build-m1/`), the guest of rungs D0 and D2 (day 3) |
| `fdt_m1_dump` | prebuilt arm64 host tool (the layer's FDT parser), covered by `SHA256SUMS`; no compiler needed on the host |
| `gfdt_dump` | prebuilt arm64 host tool (day 3): the layer's own guest-FDT builder (`el2/vgic/fdt_build.c`, E5) compiled for the host; from m1n1's prepared FDT it prints the guest FDT the layer would build (see "Day 3" below) |
| `fdt-dump-src/` | the sources of both tools plus `build.sh`; the loader builds from it only if `xcode-select -p` succeeds, never otherwise (the `cc` shim can open the Command Line Tools installer) |
| `SHA256SUMS` | the loader refuses to run if any file differs |

Built on Marco's Mac by `tools/m3a/build-m1-payload.sh [DIR]` (default `$WATARI_DATA/m3a-payload`, never inside the
repository, no network, nothing downloaded: Homebrew LLVM, `dtc` and the host `cc` already on the Mac). `WATARI_M1_EFI=<file>`
picks another EDK2 image, `WATARI_NO_EDK2=1` builds a day-2 bundle without one. **BGR8 variant (H7, optional):** `tools/build-edk2-m1.sh --bgr8` builds `WATARI_M1_EFI-bgr8.fd`; it is not in the default bundle: build a second bundle with `WATARI_M1_EFI=<that file> build-m1-payload.sh <other DIR>` (the bundle lands in `<DIR>/bundle/`).

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
| D0 | `--rung D0 --edk2 WATARI_M1_EFI.fd --fb band` | like L0 (secondaries, `kboot_prepare_dt`, read-back, `fdt_m1_dump`) with **three** unwritten 32 MB blocks (layer, test-guest spacer, EDK2): `BLOCK CHECK` over all three; then `gfdt_dump --layer <layer block> --entry <EDK2 block> --image-size <EDK2 header> --fb <mode>`: prints the guest FDT, `dtc`'s view of it when `dtc` is installed, and `GUEST FDT CHECK: PASS` (the layer's own placement rule: the first `/memory` range holds the image, the FDT and PrePi's 64 MB region and is at least 128 MB). **No jump; none of our code runs on the target** (m1n1 still does its own prepare steps, and D0 starts the secondaries like L0, which is volatile). **D0 starts the secondaries, D2 does not**: the FDT D0 previews then has 8 cpus, the one D2 prepares has 1 unless `--smp`. Run D0 with `--no-smp` unless D4 (`--smp`) is planned, so that it previews what D2 will see | the `/memory` nodes (first one contains the EDK2 block), GIC at `0xF00000000`/`0xF10000000`, framebuffer geometry and whether stride = 4 x width (`// framebuffer of the prepared FDT:` line), no Apple node or identifying property. Exit 4 = block check or placement refused: do not run D2 |
| D2 | `--rung D2 --edk2 WATARI_M1_EFI.fd --fb band --go [--cpuif trap-notc] [--smp]` | the chain layer plus EDK2 in its own block, arguments `wel2.cpuif=<..> wel2.guest=0x<EDK2 entry> wel2.abi=fdt wel2.gfdt=build wel2.fb=<mode>` (no `wel2.step`), the same read-back and checks as D0 on the real addresses (a stop on a refusal; if the FDT address cannot be captured or the header is wrong it does **not** jump: exit 3, rerun with `--fdt-addr 0x<address of the m1n1 console line 'FDT prepared at ...'>`; exit 1 if `gfdt_dump` or `fdt_m1_dump` fails for any reason other than a placement refusal (exit 1 of `gfdt_dump`, mapped to exit 4)), then the jump | H4-H7 of the PLAN; D3 is the same run continuing, D4 adds `--cpuif`/`--smp`, D5 is `--fb guest` |

D1 (test guest with check G12 and `wel2.fb=band`) is `--rung L5 --fb band` (the test guest, `wel2.fb` is appended when it is not `layer`) **[check:
that L5's check mask includes G12]**. D2 allocates the same three blocks as D0 (the second is an unwritten spacer), so the addresses D0 checked are
the ones D2 uses **[hypothesis: m1n1's heap allocates the same way twice; the read-back of D2 checks it again]**.

`gfdt_dump prepared.dtb [--layer A] [--entry E] [--image-size N] [--fb layer|band|guest] [--gran 4k|16k] [-o guest.dtb]` runs the layer's own M1 parser
(`fdt_m1_parse`) and the guest-FDT builder on the prepared FDT, so only RAM, reservations, cpu `reg` values and framebuffer geometry reach
the output; the builder refuses the identifying property names, and the tool's own scan exits 3 on any node, property or string with
`apple` in it. It also prints on stderr what it cannot know (`NOTE:` lines): without `--layer` the layer's block is not cut out of the RAM; without `--entry` the placement rules are not checked; the MSI frame is listed when `0xFFFFF000` is not guest RAM (the layer's `g_v2m_on` **[hypothesis: same rule]**). The framebuffer node is what the layer builds: `wel2_fb_plan` of `el2/vgic/fb.c` with the 16 KB granule (`--gran 4k` for `wel2.gran=4k`): for `band` the node starts below the band (`// framebuffer plan:` line with band rows, guest rows, guest base; 360 and 1240 rows for 2560 x 1600, stride 10240 **[verified: test_gfdt_dump.py, same function as the layer]**), and when the plan fails (no framebuffer, not whole pages, no room) the output says so and has no node, as the layer then keeps the panel. The self-check also scans the raw bytes of every property value for `apple`, any case. Tests: `python3 -I tools/m3a/test_gfdt_dump.py` (j293 fixture of T6b, which carries a serial number and a
MAC address on purpose).

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
