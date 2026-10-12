# ADR-0007: Strada indipendente; NTASP solo come riferimento

- Data: 2026-10-12 · Stato: **accettato** · Collegamenti: [re/ntasp-analisi.md](../../re/ntasp-analisi.md), [re/ntasp-audit-qualita.md](../../re/ntasp-audit-qualita.md), [ADR-0001](0001-boot-nativo-senza-hypervisor.md)

## Contesto
NT for Apple Silicon (NTASP, org `NT-for-ASi`) è l'unico progetto esistente con lo stesso obiettivo. Lo abbiamo analizzato (stato e architettura) e sottoposto a un audit di qualità del codice. Opus ha ricontrollato direttamente i difetti principali.
- **Architettura:** Windows gira sotto l'hypervisor di m1n1 (`hv_vgic.c`, `hv_psci.c`). Non c'è un percorso nativo e non c'è il parking protocol.
- **Estensione HAL AIC per Windows:** non esiste. Il README di `apple_silicon_nt_drivers` dice "Nothing's implemented yet".
- **Driver AIC UEFI:** la parte AICv1, quella dell'M1, è uno stub (`AicV1/AppleAicV1Dxe.c`: `//TODO: this driver.`). Il codice AICv2 ha bug: `AIC_MASK_BIT` (`AppleAicLib.h:103`) annulla mask e unmask per gli IRQ dal 5 in su.
- **Tabelle ACPI:** sono pensate per il vGIC, hanno valori inventati e alcuni indirizzi e IRQ non coincidono con il DTS di Asahi.
- **Qualità del processo:** test e CI sono assenti, i commit sono poco descrittivi e mancano header SPDX su alcuni file.

L'utente vuole riusare codice esterno solo se è di buona qualità, non solo per risparmiare tempo.

## Decisione
Watari Boot è **indipendente** e si scrive da zero.
- **Asahi Linux** (m1n1, driver Linux, DTS) è la fonte di verità sull'hardware.
- **NTASP** si usa solo come riferimento: per capire dove si sono bloccati e da quali dati partire. Nessun loro file viene copiato. I loro dati (per esempio la topologia CPU/PPTT, gli MPIDR) si usano solo dopo averli verificati con il DTS o l'ADT.
- I bug trovati si segnalano a NTASP. Primo: [apple_silicon_platforms_mu#23](https://github.com/NT-for-ASi/apple_silicon_platforms_mu/issues/23).

## Alternative scartate
- **Contribuire a NTASP:** porterebbe alla loro strada con hypervisor, in contrasto con ADR-0001, e su una base da riscrivere per circa il 70%.
- **Forkare NTASP:** erediteremmo i loro difetti, il debito e i problemi di provenienza delle licenze.

## Conseguenze
- Più lavoro iniziale sul firmware (EDK2/Mu nostro), in cambio di codice nostro, testato e con licenze pulite fin dall'inizio.
- Da subito: unit test su host, CI, commit piccoli e header SPDX su ogni file.
- Piano B (ADR-0001): l'hypervisor di NTASP resta un'alternativa esistente nel caso in cui lo spike nativo fallisca.
