<!-- SPDX-License-Identifier: MIT -->
# Optional 5-minute rung: E-HW2 inherited-state read (T18b)

To be included in the day-3 procedure. Read-only; it uses the m1n1 proxy session already used for the day-2 loader (same bundle, venv, cable). Background: `re/m3b-inherited-state.md` section 5.

## Rules that apply (unchanged from day 2)
- **Backup rule.** The rung runs only inside a day where Marco's Time Machine backup is already confirmed (as day-2 section 0 item 4: finished today, verified, confirmed in chat before any step). No separate confirmation, no run on a day without it.
- **Zero footprint on Ciani's Mac.** `ehw2.py` is not installed anywhere: it comes in the bundle (`build-m1-payload.sh` copies `ehw2.py` next to `watari-load.py`, covered by `SHA256SUMS`) and sits in `$HOSTDIR/work/m3a-payload/`, which is already listed in `$HOSTDIR/INSTALLED-host.md`. If the bundle was built before this rung existed, copy `ehw2.py` into that folder by hand and add one line to `INSTALLED-host.md` (`ehw2.py in work/m3a-payload, remove with the folder`). No `pip install`, no Homebrew, no `sudo`.

## Before (on the M1, in macOS, 1 minute, optional)
- Brightness to the maximum as usual.
- Optional NVRAM look: in Terminal **on the M1 itself** run `nvram backlight-nits` and `nvram -p | head -40`, read them on the screen (or photograph it). Nothing is redirected into `$HOSTDIR`, nothing is copied off the M1 except by hand if Marco wants it; the output can hold network names and other private data, so it never goes into the repository. Read-only: never run `nvram` with an assignment.

## Run (host, about 2 minutes)
1. Dry run (no proxy at all): `cd "$HOSTDIR/work/m3a-payload" && python3 -I ehw2.py --dry-run`.
2. Target in m1n1 proxy mode, as for the loader, with the bundle's venv `PATH` and the m1n1 proxyclient on `PYTHONPATH` (exactly the day-2 loader call):
   `PATH="$HOSTDIR/venv/bin:/usr/bin:/bin:/usr/sbin:/sbin" PYTHONPATH="$HOSTDIR/work/m1n1-main/proxyclient" python3 ehw2.py --out "$HOSTDIR/work/photos/ehw2-out.txt"` (no `-I` here: the script imports the proxyclient from `PYTHONPATH`).
3. The script sets the proxy exception guard first, reads only blocks whose PMGR power state is ACTIVE, counts every faulting read, restores the guard at the end. It writes nothing to the target. If the proxy stops answering, power-cycle the target as after any hang; nothing was written.

## After
- Read the report: ANS CPU_CONTROL bit 4 and boot status, SART ranges with the `OVERLAPS guest RAM` marks, DCP state, dart-disp0/dart-dcp lock, DART modes of the PCIe/USB masters, PMGR states, number of faulted reads.
- The report holds no serial numbers or MACs (`/chosen` and `/options` are listed by property name only); still read it before anything is committed. Put the verdicts into the day-3 results note.
- Caveat: this is the state while m1n1 serves the proxy, not the final hand-over state (the console DWC3 runs, `pcie_init` has not run). Do not use it to claim the hand-over state; a read from inside the layer is a later item.
