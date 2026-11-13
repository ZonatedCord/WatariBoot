# acpigen

C11 library and CLI that generate in binary the ACPI tables MADT (GICC, optional GICD and GICR), GTDT, PPTT, CSRT, FADT and DBG2, with correct checksums. Two models: `t8103` (13" M1 MacBook Pro, default) and `qemu-virt` (QEMU virt, GICv3, 4 CPUs).

Usage: `make -C tools/acpigen` then `build/acpigen <madt|gtdt|pptt|csrt|fadt|dbg2> [-m t8103|qemu-virt] -o file.bin`. Tests: `make -C tools/acpigen test`. Cleanup: `make clean`. CSRT exists only for t8103, DBG2 only for qemu-virt (otherwise error).

## Behavior (from `re/f0-acpi-windows-requirements.md`, table in sec. 5)
- GICC: `parking_protocol_version = 1` and a non-zero `parked_address`, 4 KB aligned, unique per CPU (BSP included). `acpigen_build_madt` returns `ACPIGEN_E_INVAL` if violated; it also checks unique UIDs and a unique `cpu_interface_number` (= original index in `cpus[]`) that is != 0xFFFFFFFF. Version 0 is allowed only with address 0 (pure PSCI).
- The boot CPU (`madt_opts.boot_index`) is the first GICC; the others follow in the original order.
- `GICC.GICR base = 1` for t8103 (like the Pi3), 0 for qemu-virt. No placeholder GICD for t8103 (`gicd = NULL`); qemu-virt has a real GICD and a GICR Structure (type 0x0E).
- qemu-virt (F-11, verified): MADT like QEMU's original one: GICD first, parking 0 (PSCI), PMU GSIV 23, GICR and **GIC MSI Frame** (0x0D, v2m 0x08020000): without the MSI Frame Windows hangs. Options `gicd_first`, `msi_*` in `acpigen_madt_opts`, `perf_gsiv` in `acpigen_cpu`.
- FADT: rev 6, 276 bytes, `HW_REDUCED_ACPI` always set, `ARM_BOOT_ARCH = 0` (PSCI not compliant, consistent with the parking).
- DBG2: one serial device (0x8000), PL011 subtype (0x0003), 32-bit/DWORD memory GAS.

## Sources and verification status
- Verified (compared with the source): layout of MADT/GICC/GICD, GTDT, PPTT in ACPI 6.5 compared with edk2 `Acpi65.h`; CSRT from the Microsoft specification of 14/11/2011 (https://cdrdv2-public.intel.com/772760/csrt.pdf); t8103 CPUs and MPIDR from AsahiLinux/linux `t8103.dtsi`.
- Verified (locally, `qemu-system-aarch64 -M virt,gic-version=3 -cpu max -smp 4 -machine dumpdtb` + `dtc`): MPIDR 0..3, GICD 0x08000000, GICR 0x080A0000 (0xF60000), timer PPI 13/14/11/10 = GSIV 29/30/27/26 with DT flag 4 (level, active high) and `always-on`, PL011 at 0x09000000 (0x1000), RAM from 0x40000000.
- Hypothesis (not verified): layout of the GICR Structure, FADT (FLAGS offset 112, ARM_BOOT_ARCH 129, minor 131, X_DSDT 140) and DBG2 (device info 22 bytes, namespace ".") derived from memory from the specification, `Acpi65.h` not available locally; QEMU's GTDT flags (in the model only ALWAYS_ON, not active-low: the DT says active high); addresses of the parking pages (t8103 0x8F0000000+, qemu-virt 0x4F000000+, to be really reserved in EDK2); t8103 boot CPU = cpu0; efficiency class; GICC.GICR = 1 on M1; t8103 timer GSIV; values of the CSRT Resource Group and its payload; whether modern Windows ARM64 still supports the parking.

## Device interrupts (GSIV) and the shared interrupt map
GSIVs of device interrupts must come from `el2/include/wel2_intr_map.h` (the same table the layer injects from: GSIV = vINTID = 32 + AIC hwirq, MSI pool at 928+), never from numbers typed here. `tests/test_gsiv.c` pins the contract with the j293 numbers (ANS 590 -> 622, DWC3 777 -> 809 / 857 -> 889, PCIe ports 695/698/701 -> 727/730/733). TODO (M3b, with the DSDT/device-entry generator, #48): acpigen has no device entries yet, so there is nothing else to generate GSIVs for; the generator should take `struct intr_map` (generated from the FDT/ADT) and emit `Interrupt(...)` resources with `intr_gsiv()` and `intr_trigger()`.

## Model `j293` (13" M1 MacBook Pro under Watari EL2, task T10)
`build/acpigen <madt|gtdt|pptt|fadt> -m j293 -o file.bin`. This model describes what the **layer** shows Windows (emulated GICv3, Arm generic timer, PSCI over HVC), not the bare SoC; `t8103` stays as the plan-A placeholder. Test: `tests/test_j293.c` (cross-checks `el2/vgic/intr_map.c`, `wel2_coreidx.h`, `wel2_apple_fiq.h`; `make test` also greps `plat.h` for the GIC window literals).

| Table | Size | Key fields | Source |
|---|---|---|---|
| MADT rev 6 | 764 B | GICD `0xF00000000` (v3); 8 GICC (UID = interface number = dense core index 0..7, enabled, PSCI so parking 0, GICC.GICR base 0, PMU GSIV 0); GICR range `0xF10000000` length `0x100000` (8 x 128 KB); GIC MSI Frame `0xFFFFF000`, flags 1, SPI base 928, count 32 | `el2/boot/plat.h`, `INTR_MAP_T8103`, `wel2_v2m.h` [verified: source files] |
| GTDT rev 3 | 104 B | virtual timer GSIV 27, NS EL1 physical GSIV 30 (always-on), secure EL1 29, NS EL2 26, all level/active-high, no platform timers | `wel2_apple_fiq.h` PPI 27/30 [verified]; 29/26 [hypothesis: ignored by Windows] |
| FADT rev 6.5 | 276 B | HW_REDUCED_ACPI, ARM_BOOT_ARCH = 3 (PSCI compliant, HVC conduit) | ACPI 6.5 FADT ARM_BOOT_ARCH flags; `el2/boot/psci.c` |
| PPTT rev 3 | 256 B | package -> cluster 0 (E, UIDs 0-3) / cluster 1 (P, UIDs 4-7) -> 8 cores, no cache nodes | cores: re/m3a-ehw0-measurements.md |

- **MPIDRs [verified: measured 2026-11-10]:** E `0x80000000..3`, P `0x80010100..3`; GICC carries the affinity fields only (`0x0..3`, `0x10100..3`). Order E then P equals the layer's `wel2_core_index()` (Aff1*4+Aff0) and the guest's VMPIDR (the layer sets `VMPIDR_EL2` = the physical MPIDR). `docs/design/watari-el2/INTERFACES.md` still says "flat Aff0 = i": superseded by the code, that doc needs updating.
- **Physical timer PPI 30:** listed because the GTDT has the NS EL1 physical timer entry. On this branch `plat_m1`'s `ppi_enable`/`fiq` hooks are still stubs (T6e), so nothing is injected on hardware yet; the shared FIQ module `apple_fiq.c` already models both gates (V -> 27, P -> 30) and design test G8 covers 30. Until T6e lands the layer never raises 30 on the M1. Whether Windows relies on 30 or on 27 is [hypothesis: unknown].
- **PMU / maintenance GSIVs 0:** the layer exposes neither [hypothesis: Windows tolerates it; QEMU's 23 is not available on the M1].
- **Caches:** no PPTT cache nodes. Geometry was not measured on day 1 and a wrong one is worse than none; Windows does not need them. Commonly quoted values (E: 128 KB I / 64 KB D, 4 MB L2; P: 192 KB I / 128 KB D, 12 MB L2; 128-byte lines) are [hypothesis, uncited]: add after reading `CLIDR_EL1`/`CCSIDR_EL1` on hardware or from the ADT.

### Console: DBG2 / SPCR choice
Windows has no inbox driver for the Apple S5L UART (`uart-1,samsung` at `0x235200000`, IRQ 605, measured row 13). Options: (a) **no DBG2/SPCR, framebuffer only** (GOP from m1n1's 2560x1600 framebuffer); (b) the layer emulates a PL011 or 16550 MMIO window and forwards bytes to the S5L UART or m1n1's dockchannel, with DBG2/SPCR pointing at it (`dbg2.c` already supports PL011 `0x0003` and 16550 `0x0000`); (c) expose the real S5L UART: rejected, its register layout is not 16550 [hypothesis].
**Decision: (a) for now.** With nothing Windows can drive, a DBG2/SPCR entry would only make it try to load a debug transport that cannot bind [hypothesis]; EDK2 logs go to the S5L UART/m1n1 without ACPI; the layer has its own log channel. When kernel debugging is wanted, (b) with a layer-emulated PL011 is the cheapest path (the PL011 DBG2 path is already tested against QEMU). The generator returns `E_INVAL` for `dbg2 -m j293` on purpose (tested). No CSRT either (a plan-A artefact).
