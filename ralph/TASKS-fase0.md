# Fase 0: task per Ralph

Questa lista la modifica solo Ralph, sul branch `ralph/f0-gate-a`. Si procede in ordine: si prende il primo task non spuntato.
Formato: `- [ ]` da fare, `- [x]` fatto e verificato da Opus, `- [!]` bloccato (motivo in `ralph/PROGRESS-fase0.md`), `- [→]` assegnato a Ciani: **Ralph non lo tocca**.

## Gate A: niente download, niente installazioni (si possono fare SUBITO)
Strumenti ammessi: lettura web (WebSearch e WebFetch), `gh api` in sola lettura, scrittura di file nella repo, e gli strumenti già installati (`clang`, `make`, `python3 -I`, `git`).

- [→] *(Ciani, issue #4: Ralph NON lo fa)* **A1. Estensioni HAL: documentazione pubblica Microsoft.**
  Cosa sono, come vengono caricate (tabella CSRT, ID delle risorse), quali interfacce esistono (timer, DMA, *interrupt controller?*), requisiti di firma e test-signing.
  Fonti ammesse: Microsoft Learn, specifica CSRT, WDK docs, HLK.
  Output: `re/f0-halext-doc-pubblica.md`.
  Criterio di completamento: ogni affermazione ha una fonte. La domanda *"un'estensione HAL può fornire un controller di interrupt?"* ha una risposta [verificato] oppure è dichiarata non risolta da documentazione pubblica.
- [→] *(Ciani, issue #5: Ralph NON lo fa)* **A2. Requisiti ACPI di Windows ARM64.**
  Tabelle obbligatorie e campi minimi: MADT/GICC, GTDT, FADT, DBG2, SPCR, PPTT. Requisiti BSA/SBSA che Windows impone. Specifica Microsoft del *Multiprocessor Parking Protocol* (versione, layout della mailbox, come si sveglia un core).
  Output: `re/f0-acpi-requisiti-windows.md`.
- [x] *(issue #6)* **A3. M1: sorgenti FIQ e alternative in IRQ.**
  Dal codice Asahi (`irq-apple-aic.c`, m1n1 `aic.c`/`hv_*`, `arch_timer`): elenco di tutte le sorgenti FIQ del t8103 (timer fisico e virtuale, Fast IPI, PMU, uncore). Per ognuna: si può disabilitare o instradare in IRQ? Esistono timer in MMIO alternativi sul SoC? Come funzionano le IPI AIC in IRQ?
  Output: `re/f0-m1-fiq-irq.md`, con file e righe Asahi citati.
- [→] *(Ciani, issue #7: Ralph NON lo fa)* **A4. Il precedente Raspberry Pi 3 (senza GIC).**
  Come girava Windows (IoT Core / ARM64) sul BCM2836/7 senza GIC, ricostruito da fonti pubbliche: `ms-iot/rpi-iotcore`, `raspberrypi/windows-drivers`, `pftf/RPi3`, `worproject`, forum, ACPI pubblicate (MADT/CSRT del Pi 3).
  Output: `re/f0-rpi3-precedente.md`.
  Criterio di completamento: è chiaro, con fonti, se esisteva un'estensione HAL per il controller di interrupt, oppure è dichiarato non determinabile.
- [ ] *(issue #8)* **A5. Progetto dell'esperimento in QEMU (Gate B).**
  Matrice delle varianti ACPI da provare: (1) baseline con GIC; (2) MADT senza GICD; (3) GICC con il parking protocol; (4) CSRT con un gruppo di risorse nostro. Per ogni variante: cosa si osserva (bugcheck code, output del debugger, se l'estensione viene caricata) e cosa si conclude. Includere il setup di debug (KDNET/seriale) e l'ambiente per compilare un'estensione HAL di prova (WDK in una VM Windows ARM, solo come strumento di sviluppo).
  Output: `docs/design/watariboot/ESPERIMENTO-F0.md`.
  Se i file di A1, A2 e A4 (di Ciani) non sono ancora su `main`, scrivi l'esperimento basandoti su `docs/PLAN.md` e su `docs/design/watariboot/INTERFACES.md`, e segna come [ipotesi] i punti che dipendono da loro.
- [x] *(issue #9)* **A6. `tools/acpigen`: generatore di tabelle ACPI su host, con test.**
  Libreria C11 + CLI che, a partire da un modello dati (C o JSON), produce MADT (GICC con i campi del parking protocol), GTDT, PPTT e CSRT in binario, con checksum corretti.
  - Unit test compilati con `clang` + `make`, già presenti sul sistema: layout e offset dei campi verificati contro la specifica, checksum, casi limite.
  - Dati t8103 (cluster, MPIDR) presi dal DTS di Asahi via `gh api` e citati.
  - Header SPDX su ogni file.
  Criterio di completamento: `make -C tools/acpigen test` verde.
- [ ] *(issue #10)* **A7. Verdetto preliminare.** **Prerequisito:** `re/f0-halext-doc-pubblica.md`, `re/f0-acpi-requisiti-windows.md` e `re/f0-rpi3-precedente.md` devono essere su `main` (le PR di Ciani). Se mancano, segna `[!]` con il motivo "in attesa di Ciani" e non scrivere niente.
  Sintesi di A1–A4 in `re/f0-verdetto-preliminare.md`: cosa si può già concludere, cosa resta da dimostrare solo con l'esperimento A5, con quali probabilità. Proposta di ADR in `docs/adr/bozza-esito-fase0-preliminare.md`.

## Gate B: richiede backup confermato + almeno 100 GB liberi + l'ok umano (Ralph NON li esegue)
- [ ] B1. Installare qemu, acpica-tools e le dipendenze di EDK2. [UMANO conferma]
- [ ] B2. Build di EDK2 `ArmVirtQemu` e boot fino alla shell UEFI in QEMU.
- [ ] B3. ISO ufficiale di Windows 11 ARM64 dal sito Microsoft. [UMANO: download dal browser]
- [ ] B4. Baseline: Windows ARM64 avviato in QEMU con il nostro EDK2 e le tabelle di `acpigen`.
- [ ] B5. Le varianti della matrice A5, con le evidenze raccolte.
- [ ] B6. VM di sviluppo Windows ARM con Visual Studio + WDK. Estensione HAL di prova, test-signed. [UMANO: setup e licenze]
