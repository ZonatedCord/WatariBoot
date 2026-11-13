# Watari EL2 hardware-day bundle (M3a, first boot of the layer on the M1)

Everything here is RAM only on the target: images, the DTB and m1n1's FDT copy are written into m1n1's heap through the
USB proxy; a power cycle removes it all. Nothing persistent is touched (no NVMe, NVRAM, PMU, boot policy).
Claims are marked **[verified: source]** or **[hypothesis]**; everything not yet run on the hardware is **[check]**.

## What is in the bundle

| File | What |
|---|---|
| `wel2-m1.bin` | the layer, M1 build with the built-in EL1 payload (rungs L1, L2) |
| `wel2-chain.bin` | the layer, chain build: enters the guest named by `wel2.guest` at EL1 (rungs L3-L5) |
| `m1guest.bin` | EL1 test guest, checks G1-G11 (rungs L3-L5) |
| `j293-minimal.dts/.dtb` | minimal j293 device tree that m1n1's `kboot_prepare_dt` fills (DESIGN 4.2) |
| `watari-load.py` | the host loader |
| `fdt-dump-src/` | `fdt_m1_dump` as plain C plus `build.sh` (host `cc`, no other dependency) |
| `SHA256SUMS` | the loader refuses to run if any file differs |

Built on Marco's Mac by `tools/m3a/build-m1-payload.sh [DIR]` (default `$WATARI_DATA/m3a-payload`, never inside the
repository, no network).

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
| L0 | `--rung L0` | writes the DTB, sets no wel2 arguments, `kboot_prepare_dt`, reads the prepared FDT back, saves it in `out/`, runs `fdt_m1_dump` on it. **No jump.** | `kboot_prepare_dt OK`, then `platform m1 (apple,aic found)`, 8 cpus with `release_addr` set for the secondaries, memory/usable/reserved ranges, aic, uart, wdt, framebuffer 2560x1600. Proxy still alive. |
| L1 | `--rung L1 --go` | `wel2-m1.bin`, `wel2.step=1`: hand-over, the layer masks DAIF, relocates itself, writes the watchdog reset | the **Mac restarts within about a second**. No display needed. |
| L2 | `--rung L2 --go` | `wel2-m1.bin`, `wel2.step=2`, `wel2.cpuif=trap`: MMU, FDT parse, UART, AIC/FIQ init, banner, then watchdog reset | squares 0.. green and the status band on the panel, then a restart |
| L3 | `--rung L3 --go [--cpuif trap-notc]` | `wel2-chain.bin` + `m1guest.bin`, `wel2.step=3 wel2.cpuif=<..> wel2.guest=0x<guest block>`: stage 2, guest G1-G6 on one core. Run once with `trap`, once with `trap-notc` | band shows `GUEST PASS n FAIL 0`; reset at the end |
| L4 | `--rung L4 --go` | L3 plus timers G7-G8 | as L3 |
| L5 | `--rung L5 --go` | L4 plus the secondaries started by the host (`smp_start_secondaries`), G9-G10 | as L4 |

Layer and guest are each placed at the start of their own 32 MB-aligned, 32 MB block from m1n1's heap (D-2). The hand-over
(`wel2.guest` entry, guest RAM visible to the layer) for the M1 is T10/T11 work: **[hypothesis]** that L3-L5 reach
the guest on the first try; the layer says so itself with a FATAL line if the info block is not in guest RAM.

`--dry-run` works for every rung and opens no device. `--args "wel2.xxx=..."` appends boot arguments. `--no-smp` / `--smp`
override the secondary start. `--boot-timeout` (default 5 s) is how long the loader waits for a reply that may never come.

### Call order (asserted by `test_watari_load.py`)

verify sums -> `malloc` DTB + write -> `memalign` 32 MB (layer, then guest) + write -> `dc_cvau`/`ic_ivau` over each image
-> **then** `kboot_set_chosen("bootargs", ...)` (the arguments contain the guest address, DESIGN L-2) -> `smp_start_secondaries`
(L5) -> `kboot_prepare_dt` -> L0 stops (read back FDT, `fdt_m1_dump`) -> DAIF = 0xc0 -> `kboot_boot`. A timeout or
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

- Abort criteria: L1 no restart within 5 s; later rungs 60 s without a new checkpoint, a red square, any `FATAL`/`unexpected` text.
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
waiting for replies). Neither is committed or pasted into chat; `fdt_m1_dump` prints only what the layer uses. The loader
echoes only m1n1 console lines about the FDT address and failures. The folder is deleted at the end of the day.
