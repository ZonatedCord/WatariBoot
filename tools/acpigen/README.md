# acpigen

Libreria C11 e CLI che generano in binario le tabelle ACPI MADT (GICC, GICD e GICR opzionali), GTDT, PPTT, CSRT, FADT e DBG2, con checksum corretti. Due modelli: `t8103` (MacBook Pro 13" M1, predefinito) e `qemu-virt` (QEMU virt, GICv3, 4 CPU).

Uso: `make -C tools/acpigen` poi `build/acpigen <madt|gtdt|pptt|csrt|fadt|dbg2> [-m t8103|qemu-virt] -o file.bin`. Test: `make -C tools/acpigen test`. Pulizia: `make clean`. CSRT esiste solo per t8103, DBG2 solo per qemu-virt (altrimenti errore).

## Comportamento (da `re/f0-acpi-windows-requirements.md`, tabella sez. 5)
- GICC: `parking_protocol_version = 1` e `parked_address` non zero, allineato 4 KB, unico per CPU (BSP compresa). `acpigen_build_madt` ritorna `ACPIGEN_E_INVAL` se violato; controlla anche UID unici e `cpu_interface_number` (= indice originale in `cpus[]`) unico e != 0xFFFFFFFF. Versione 0 ammessa solo con indirizzo 0 (PSCI puro).
- La CPU di boot (`madt_opts.boot_index`) e' il primo GICC; le altre seguono nell'ordine originale.
- `GICC.GICR base = 1` per t8103 (come Pi3), 0 per qemu-virt. Nessuna GICD segnaposto per t8103 (`gicd = NULL`); qemu-virt ha GICD reale e una GICR Structure (tipo 0x0E).
- qemu-virt (F-11, verificato): MADT come quella originale di QEMU: GICD per prima, parking 0 (PSCI), PMU GSIV 23, GICR e **GIC MSI Frame** (0x0D, v2m 0x08020000): senza la MSI Frame Windows si blocca. Opzioni `gicd_first`, `msi_*` in `acpigen_madt_opts`, `perf_gsiv` in `acpigen_cpu`.
- FADT: rev 6, 276 byte, `HW_REDUCED_ACPI` sempre attivo, `ARM_BOOT_ARCH = 0` (PSCI non conforme, coerente col parking).
- DBG2: un dispositivo seriale (0x8000), sottotipo PL011 (0x0003), GAS memoria 32 bit/DWORD.

## Fonti e stato di verifica
- Verificato (confrontato con la fonte): layout di MADT/GICC/GICD, GTDT, PPTT in ACPI 6.5 confrontati con edk2 `Acpi65.h`; CSRT dalla specifica Microsoft del 14/11/2011 (https://cdrdv2-public.intel.com/772760/csrt.pdf); CPU e MPIDR t8103 da `t8103.dtsi` di AsahiLinux/linux.
- Verificato (in locale, `qemu-system-aarch64 -M virt,gic-version=3 -cpu max -smp 4 -machine dumpdtb` + `dtc`): MPIDR 0..3, GICD 0x08000000, GICR 0x080A0000 (0xF60000), timer PPI 13/14/11/10 = GSIV 29/30/27/26 con flag DT 4 (level, active high) e `always-on`, PL011 a 0x09000000 (0x1000), RAM da 0x40000000.
- Ipotesi (non verificate): layout di GICR Structure, FADT (offset FLAGS 112, ARM_BOOT_ARCH 129, minor 131, X_DSDT 140) e DBG2 (device info 22 byte, namespace ".") ricavati a memoria dalla specifica, `Acpi65.h` non disponibile in locale; flag GTDT di QEMU (nel modello solo ALWAYS_ON, non active-low: il DT dice active high); indirizzi delle pagine di parking (t8103 0x8F0000000+, qemu-virt 0x4F000000+, da riservare davvero nell'EDK2); boot CPU t8103 = cpu0; efficiency class; GICC.GICR = 1 su M1; GSIV timer t8103; valori del Resource Group CSRT e suo payload; se Windows ARM64 moderno supporti ancora il parking.
