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
