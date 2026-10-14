# F-11: confronto ACPI originale QEMU vs acpigen `qemu-virt`

Stato: **verificato** (tabelle originali lette dalla RAM del guest, nostre tabelle da `acpigen`, entrambe disassemblate con `iasl -d`, 2026-10-14).

## Come si sono ottenute le tabelle originali
Firmware originale (`ArmVirtQemu`, senza il nostro driver), `qemu-system-aarch64 -M virt,gic-version=3 -accel hvf -cpu host -smp 4 -m 6G`, 45 s di avvio fino alla UEFI, poi dal monitor `pmemsave 0x40000000 0x30000000 ram.bin`. Ricerca di `RSD PTR ` (a 0x47FF3018), XSDT, tabelle: FACP, APIC (MADT), PPTT, GTDT, MCFG, SPCR, DBG2, IORT. Le tabelle sono state estratte e disassemblate in `/tmp/watari-data/run-f11/d/`.

## MADT (originale: rev 4, 428 byte; la nostra prima: rev 6, 412 byte; la finale: 436 byte)
| Campo | Originale QEMU | Nostra (prima) | Nostra (finale) |
|---|---|---|---|
| Ordine sottotabelle | GICD, 4 GICC, GICR, MSI Frame | 4 GICC, GICD, GICR | come l'originale |
| Revisione | 4 | 6 | 6 (benigna) |
| GICC lunghezza | 0x50 (80) | 0x52 (82) | 82 (benigna) |
| GICC CPU interface / UID / MPIDR | 0..3 / 0..3 / 0..3 | uguali | uguali |
| GICC flags | 1 (enabled) | 1 | 1 |
| GICC parking version / parked address | 0 / 0 | 1 / 0x4F000000+0x1000*i | 0 / 0 (H1) |
| GICC performance GSIV | 0x17 (23) | 0 | 23 |
| GICC base/GICV/GICH/GICR base | 0 | 0 | 0 |
| GICD base / id / version | 0x08000000 / 0 / 3 | uguali | uguali |
| GICR Structure (0x0E) | base 0x080A0000, len 0xF60000 | uguale | uguale |
| **GIC MSI Frame (0x0D, 24 byte)** | **id 0, base 0x08020000, flags 1 (SPI select), SPI count 0x40, SPI base 0x50** | **ASSENTE** | presente, uguale |
| ITS (0x0F) | assente (QEMU con HVF non ha l'ITS, usa il v2m) | assente | assente |
| OEM / creator | BOCHS / BXPC | WATARI / WTRB | WATARI / WTRB (benigna) |

Dopo H5 il confronto `xxd` mostra solo: revisione, lunghezza GICC (con lo slittamento di offset che ne deriva), OEM.

## GTDT (originale rev 3, 104 byte)
| Campo | Originale | Nostra (prima) | Nostra (finale) |
|---|---|---|---|
| Counter block / read block | 0xFFFF... | uguale | uguale |
| Secure EL1 GSIV / flags | 29 / 0 | 29 / 4 | 29 / 0 |
| NS EL1 GSIV / flags | 30 / 4 (always on) | 30 / 4 | 30 / 4 |
| Virtual GSIV / flags | 27 / 0 | 27 / 4 | 27 / 0 |
| NS EL2 GSIV / flags | 26 / 0 | 26 / 4 | 26 / 0 |
| Virtual EL2 GSIV / flags | 0 / 0 | 0 / 0 | 0 / 0 |
Finale: identica byte per byte all'originale, tranne OEM id/checksum. Le differenze di flag non erano la causa (H3, e con ALWAYS_ON ovunque + MSI Windows parte).

## Altre tabelle (non sostituite)
FADT: PSCI compliant = 1, HVC = 1, HW_REDUCED = 1 (quindi con PSCI il parking non e' usato). DBG2 originale 87 byte, la nostra 84: non installata. PPTT, IORT, MCFG, SPCR: lasciate originali.

## Conclusione
Causa di F-11: la **GIC MSI Frame mancante**. Ablazione (`docs/FALLIMENTI.md`): H1, H2, H3 da soli o insieme non bastano; MSI Frame da sola basta (anche con parking v1, GICD dopo i GICC e GTDT tutti ALWAYS_ON).
