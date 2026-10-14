# F-11: original QEMU ACPI vs acpigen `qemu-virt`

Status: **verified** (original tables read from the guest RAM, our tables from `acpigen`, both disassembled with `iasl -d`, 2026-10-14).

## How the original tables were obtained
Original firmware (`ArmVirtQemu`, without our driver), `qemu-system-aarch64 -M virt,gic-version=3 -accel hvf -cpu host -smp 4 -m 6G`, 45 s of boot up to the UEFI, then from the monitor `pmemsave 0x40000000 0x30000000 ram.bin`. Search for `RSD PTR ` (at 0x47FF3018), XSDT, tables: FACP, APIC (MADT), PPTT, GTDT, MCFG, SPCR, DBG2, IORT. The tables were extracted and disassembled in `/tmp/watari-data/run-f11/d/`.

## MADT (original: rev 4, 428 bytes; our first: rev 6, 412 bytes; the final: 436 bytes)
| Field | Original QEMU | Ours (first) | Ours (final) |
|---|---|---|---|
| Subtable order | GICD, 4 GICC, GICR, MSI Frame | 4 GICC, GICD, GICR | same as the original |
| Revision | 4 | 6 | 6 (benign) |
| GICC length | 0x50 (80) | 0x52 (82) | 82 (benign) |
| GICC CPU interface / UID / MPIDR | 0..3 / 0..3 / 0..3 | same | same |
| GICC flags | 1 (enabled) | 1 | 1 |
| GICC parking version / parked address | 0 / 0 | 1 / 0x4F000000+0x1000*i | 0 / 0 (H1) |
| GICC performance GSIV | 0x17 (23) | 0 | 23 |
| GICC base/GICV/GICH/GICR base | 0 | 0 | 0 |
| GICD base / id / version | 0x08000000 / 0 / 3 | same | same |
| GICR Structure (0x0E) | base 0x080A0000, len 0xF60000 | same | same |
| **GIC MSI Frame (0x0D, 24 bytes)** | **id 0, base 0x08020000, flags 1 (SPI select), SPI count 0x40, SPI base 0x50** | **ABSENT** | present, same |
| ITS (0x0F) | absent (QEMU with HVF has no ITS, uses the v2m) | absent | absent |
| OEM / creator | BOCHS / BXPC | WATARI / WTRB | WATARI / WTRB (benign) |

After H5 the `xxd` comparison shows only: revision, GICC length (with the resulting offset shift), OEM.

## GTDT (original rev 3, 104 bytes)
| Field | Original | Ours (first) | Ours (final) |
|---|---|---|---|
| Counter block / read block | 0xFFFF... | same | same |
| Secure EL1 GSIV / flags | 29 / 0 | 29 / 4 | 29 / 0 |
| NS EL1 GSIV / flags | 30 / 4 (always on) | 30 / 4 | 30 / 4 |
| Virtual GSIV / flags | 27 / 0 | 27 / 4 | 27 / 0 |
| NS EL2 GSIV / flags | 26 / 0 | 26 / 4 | 26 / 0 |
| Virtual EL2 GSIV / flags | 0 / 0 | 0 / 0 | 0 / 0 |
Final: byte-for-byte identical to the original, except OEM id/checksum. The flag differences were not the cause (H3, and with ALWAYS_ON everywhere + MSI Windows boots).

## Other tables (not replaced)
FADT: PSCI compliant = 1, HVC = 1, HW_REDUCED = 1 (so with PSCI parking is not used). Original DBG2 87 bytes, ours 84: not installed. PPTT, IORT, MCFG, SPCR: left original.

## Conclusion
Cause of F-11: the **missing GIC MSI Frame**. Ablation (`docs/FAILURES.md`): H1, H2, H3 alone or together are not enough; MSI Frame alone is enough (even with parking v1, GICD after the GICCs and GTDT all ALWAYS_ON).
