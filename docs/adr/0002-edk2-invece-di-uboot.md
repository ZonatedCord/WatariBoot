# ADR-0002: EDK2 invece di U-Boot

- Stato: Accettata (dal piano)
- Data: 2026-10-12 · Collegamenti: [ARCHITECTURE](../design/provaimpossibile/ARCHITECTURE.md), [INTERFACES](../design/provaimpossibile/INTERFACES.md)

## Contesto
Windows ARM64 richiede UEFI e ACPI [ipotesi: requisito dal piano, non ricontrollato su documentazione Microsoft]. Servono GOP, servizi runtime UEFI, ACPI, parking protocol e CSRT. Il piano indica `ArmVirtPkg` e `Platform/RaspberryPi` di edk2-platforms come modelli.

## Decisione
Usare una piattaforma EDK2 (`AppleSiliconPkg`) avviata da m1n1, con ACPI generate dall'ADT.

## Alternative
- U-Boot con UEFI: esiste un supporto UEFI e il parking protocol in patch U-Boot [verificato: lista U-Boot denx.de, "arm: cpu: Add ACPI parking protocol support"], ma il supporto UEFI/ACPI di U-Boot per avviare Windows non e' stato valutato qui [ipotesi]. Scartato nel piano; la motivazione dettagliata non e' documentata e va completata se si riapre la scelta.
- Bootloader custom: scartato, costo di mantenimento alto.
- Usare direttamente m1n1 come UEFI: non esistente.

## Conseguenze
- Si eredita l'ecosistema EDK2 (build, driver USB/GOP/ACPI) e la sua complessita' di build.
- Modello esistente per Windows su hardware non standard (Raspberry Pi) [verificato: citato nel piano, non riscontrato qui].
- Il contratto m1n1 -> EDK2 va definito da noi (INTERFACES 1).
