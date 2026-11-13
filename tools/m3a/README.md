# tools/m3a: m1n1 proxyclient scripts for the real M1 (M3a)

Run from the host Mac with the m1n1 proxyclient on the path and the target in m1n1 proxy mode:
`M1N1DEVICE=$(ls /dev/cu.usbmodem*1) PYTHONPATH=<m1n1>/proxyclient python3 <script> [out.txt]`.
The scripts import the external m1n1 proxyclient (GPL-2.0, not vendored here); run them from a separate m1n1 checkout as described in `docs/procedures/m3a-day1-hardware.md`.
None of them imports `m1n1.setup` (it writes the PMU). Results: `re/m3a-ehw0-measurements.md`.

| Script | What it does | Writes on the target |
|---|---|---|
| `ehw0.py`, `ehw0b.py` | E-HW0 rows 1-14: system registers, ADT nodes, AIC reads (never EVENT) | none |
| `ehw0c.py` | starts the secondaries, MPIDR/MIDR/WHOAMI on all 8 cores | secondary start (volatile) |
| `daif_check.py`, `el1_ctrl.py`, `ping.py` | DAIF per core, EL1 entry control, is m1n1 alive | none |
| `e2h_test.py` | HCR_EL2.E2H write-0 test on cpu7, restores | HCR_EL2 (volatile) |
| `aic_qd5.py` | AIC SW_SET / mask / EVENT behaviour on unused IRQ 891 routed to core 7, restores | AIC registers (volatile) |
| `vgic_test.py`, `vgic_test0.py`, `vgic_masked.py` | vGIC list-register delivery to EL1 (hangs m1n1 afterwards: power cycle) | ICH_* (volatile) |
| `build-m1-payload.sh`, `watari-load.py`, `test_watari_load.py`, `j293-minimal.dts` | day-2 bundle (layer, guest, minimal DTB, loader, `fdt_m1_dump` source, SHA256SUMS), the host loader (rungs L0-L5, `--dry-run`), its fake-proxy test (`python3 -I tools/m3a/test_watari_load.py`); see `README-m1-payload.md` | RAM only: DTB, images, m1n1's FDT copy; the jump rungs leave m1n1 |
| `demo/scimmietta.py` | screen demo of 2026-11-10: dancing monkey in a nappy eating bananas | framebuffer RAM |
