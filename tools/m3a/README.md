# tools/m3a: m1n1 proxyclient scripts for the real M1 (M3a)

Run from the host Mac with the m1n1 proxyclient on the path and the target in m1n1 proxy mode:
`M1N1DEVICE=$(ls /dev/cu.usbmodem*1) PYTHONPATH=<m1n1>/proxyclient python3 <script> [out.txt]`.
None of them imports `m1n1.setup` (it writes the PMU). Results: `re/m3a-ehw0-measurements.md`.

| Script | What it does | Writes on the target |
|---|---|---|
| `ehw0.py`, `ehw0b.py` | E-HW0 rows 1-14: system registers, ADT nodes, AIC reads (never EVENT) | none |
| `ehw0c.py` | starts the secondaries, MPIDR/MIDR/WHOAMI on all 8 cores | secondary start (volatile) |
| `daif_check.py`, `el1_ctrl.py`, `ping.py` | DAIF per core, EL1 entry control, is m1n1 alive | none |
| `e2h_test.py` | HCR_EL2.E2H write-0 test on cpu7, restores | HCR_EL2 (volatile) |
| `aic_qd5.py` | AIC SW_SET / mask / EVENT behaviour on unused IRQ 891 routed to core 7, restores | AIC registers (volatile) |
| `vgic_test.py`, `vgic_test0.py`, `vgic_masked.py` | vGIC list-register delivery to EL1 (hangs m1n1 afterwards: power cycle) | ICH_* (volatile) |
| `logo_screen.py` | draws `tools/logo` on the screen | framebuffer RAM |
| `demo/` | screen demos of 2026-11-10: Christmas tree (`albero*.py`), live CNTPCT counter (`live.py`), banner (`banner.py`), dancing monkey in a nappy eating bananas (`scimmietta.py`) | framebuffer RAM |
