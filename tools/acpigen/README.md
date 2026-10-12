# acpigen

Libreria C11 e CLI che generano in binario le tabelle ACPI MADT (GICC, GICD opzionale), GTDT, PPTT e CSRT, con checksum corretti. Il modello dati di esempio e' il t8103 (`src/t8103_model.c`).

Uso: `make -C tools/acpigen` poi `build/acpigen <madt|gtdt|pptt|csrt> -o file.bin`. Test: `make -C tools/acpigen test`. Pulizia: `make clean`.

Fonti dei layout: ACPI 6.5 (MADT 5.2.12, GTDT 5.2.24, PPTT 5.2.30) confrontata con edk2 `MdePkg/Include/IndustryStandard/Acpi65.h`; CSRT dalla specifica Microsoft del 14/11/2011 (https://cdrdv2-public.intel.com/772760/csrt.pdf); CPU e MPIDR da `arch/arm64/boot/dts/apple/t8103.dtsi` di AsahiLinux/linux.

Ipotesi (non verificate): parked address e versione del parking protocol, GSIV dei timer, base e versione della GICD, efficiency class, valori del Resource Group CSRT e suo payload. Vedi i commenti in `t8103_model.c`.
