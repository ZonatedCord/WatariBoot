<!-- SPDX-License-Identifier: MIT -->
# Optional 5-minute rung: E-HW2 inherited-state read (T18b)

To be included in the day-3 procedure. Read-only; needs the m1n1 proxy session already used for `ehw0.py` (same venv, same cable, same host rules). Background: `re/m3b-inherited-state.md` section 5.

## Before (on the target, in macOS, 1 minute, optional)
- Set the brightness to the maximum as usual. Optionally run `nvram -p > $HOSTDIR/work/nvram-private.txt` (read only; the file stays on the host, never committed: it can hold network names and other private data). Never run `nvram` with a variable assignment.

## Run (host, about 2 minutes)
1. Dry run first (no proxy at all): `python3 -I tools/m3a/ehw2.py --dry-run`.
2. With the target in m1n1 proxy mode (same state as for ehw0): `python3 tools/m3a/ehw2.py --out $HOSTDIR/work/ehw2-out.txt` (not `-I`: the script imports the m1n1 proxyclient from the venv). Same `PATH` limitation as for the day-2 proxyclient (F-32).
3. The script sets the proxy exception guard first, reads only blocks whose PMGR power state is ACTIVE, restores the guard at the end. It writes nothing to the target. If the proxy stops answering, power-cycle the target as after any hang; nothing was written.

## After
- Read the report: ANS CPU_CONTROL bit 4 and boot status, SART ranges with the `OVERLAPS guest RAM` marks, DCP state, dart-disp0/dart-dcp lock, DART modes of the PCIe/USB masters, PMGR states.
- The output holds no serial numbers or MACs (`/chosen` and `/options` are listed by property name only); still check it before it is committed. Keep the file on the host; put the verdicts into the day-3 results note.
- Caveat: this is the state while m1n1 serves the proxy, not the final hand-over state (the console DWC3 runs, `pcie_init` has not run). Do not use it to claim hand-over state; a read from inside the layer is a later item.
