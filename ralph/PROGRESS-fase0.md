# Fase 0: diario di Ralph

Ralph aggiunge una voce per ogni task: data, task, file prodotti, cosa ha verificato Opus, dubbi aperti.

## Voci

### 2026-10-12: A3 completato (issue #6)
- File: `re/f0-m1-fiq-irq.md` (Asahi linux@asahi 77cb8f24c2, m1n1 main fbec52a81f).
- **Verificato da Opus** sul DTS `t8103.dtsi@asahi`:
  - (1) il timer `arm,armv8-timer` ha i 4 interrupt tutti `AIC_FIQ` (righe 369-375);
  - (2) il watchdog `watchdog@23d2b0000` ha `AIC_IRQ 338` (righe 1223-1228).
- **Scoperta critica:** il timer architetturale è **solo FIQ**, e non c'è nessun modo documentato di instradarlo in IRQ. Tutte le altre sorgenti FIQ (Fast IPI, PMU, uncore) si possono spegnere alla fonte. Se Windows richiede il timer architetturale, la strategia "niente FIQ" (ADR-0004) cade. Piste da verificare:
  - (a) bit `ACC_OVRD` "CONVERT_SIQ_TO_IRQ", definito in m1n1 ma mai usato [ipotesi];
  - (b) il watchdog come timer di sistema tramite un'estensione HAL timer: un solo comparatore per tutto il SoC, contatore a 32 bit che fa il giro in circa 179 s [ipotesi].
- **IPI:** quelle legacy dell'AIC in IRQ funzionano su AICv1 (codice storico di Linux), ma **AICv2 (M1 Pro/Max e successivi) non le ha**, quindi la strategia non vale per tutti gli Apple Silicon.
- **Dubbi aperti:** discrepanze sui campi di IPI_RR tra Linux e la doc Asahi; nessuna misura di latenza.

### 2026-10-12: A6 completato (issue #9)
- File: `tools/acpigen/` (libreria C11 + CLI + test). Genera MADT (GICC/GICD), GTDT, PPTT e CSRT.
- **Verificato da Opus:** `make -C tools/acpigen test` → `TUTTI I TEST OK` (rieseguito); GICC da 82 byte coerente con ACPI 6.5.
- **Ipotesi** (marcate nei sorgenti): parked address/version = 0, classi di efficienza, GSIV dei timer (segnaposto PPI), ID CSRT "APLE"/0x8103, MPIDR = `reg` del DTS.
- **Nota:** il layout CSRT viene dal PDF della specifica Microsoft (link da learn.microsoft.com), letto dall'agente. Non è rimasta nessuna copia del PDF nel repository.

### 2026-10-12: A5 completato (issue #8)
- File: `docs/design/watariboot/ESPERIMENTO-F0.md` (matrice V0–V5, debug, prerequisiti, rischi).
- **Verificato da Opus** sulla documentazione QEMU:
  - (1) `gic-version` accetta solo 2/3/4/x-5/host/max: il GIC non si può rimuovere, e il "non-GIC" si simula solo nelle tabelle ACPI (qemu.org, system/arm/virt);
  - (2) `-acpitable` vale solo per il target i386 (qemu.org, system/invocation). Le nostre tabelle vanno iniettate con un driver DXE in EDK2.
- **Conseguenze:**
  - `acpigen` va esteso (modello `qemu-virt`, `--no-gicd`, `--parking`, GICR, FADT, SSDT): nuovo lavoro per il Gate B;
  - lo spazio stimato per il Gate B è di 75–115 GiB (oggi ne sono liberi circa 53–56);
  - QEMU dà un verdetto negativo forte ma solo un positivo debole: non prova niente su AIC, FIQ o risveglio reale del M1.
- **Ipotesi aperte:** sintassi `-serial`/`-chardev`/`-netdev`, e se `e1000e` di QEMU sia compatibile con KDNET.

### 2026-10-12: A7 bloccato (issue #10)
- Motivo: **in attesa di Ciani**. `re/f0-halext-doc-pubblica.md`, `re/f0-acpi-requisiti-windows.md` e `re/f0-rpi3-precedente.md` non sono ancora su `main` (controllato su `origin/main`).
- Si riprende in una nuova sessione o con un nuovo loop, dopo il merge delle PR di Ciani.

## Riepilogo finale del Gate A (questo giro di Ralph)
- Completati: **A3** (#6), **A5** (#8), **A6** (#9).
- Bloccato: **A7** (#10), in attesa di A1, A2 e A4 (Ciani: #4, #5, #7).
- **Scoperte principali:**
  1. Il timer architetturale del M1 è solo FIQ. È un rischio critico per ADR-0004 e va verificato con V5 in QEMU e con A1 (le estensioni HAL timer).
  2. In QEMU il GIC non si può rimuovere: gli esperimenti danno solo un "no" affidabile, non un "sì".
  3. Per il Gate B servono 75–115 GiB e le estensioni ad `acpigen` (modello qemu-virt, FADT/SSDT, `--no-gicd`, `--parking`).

## Secondo giro (branch `ralph/f0-gate-a2`, dal 2026-10-13)
- Task: A8 (#16), A9 (#17), A10 (#18). A7 resta `[!]` in attesa di Ciani.

### 2026-10-13: A8 completato (#16), modelli AIC in QEMU
- File: `re/f0-qemu-aic-modelli.md`.
- **Verificato da Opus:**
  - `TrungNguyen1909/qemu-t8030` esiste (archiviato, 2204 stelle, ultimo push 2022-10). Il suo `hw/intc/apple_aic.c` usa gli offset di AICv1 (0x2004 IACK, 0x3000 DEST, 0x4100 MASK_SET).
  - `ChefKissInc/Inferno` è attivo (push 2026-09).
  - La licenza del repo t8030 risulta "NOASSERTION" su GitHub.
- **Raccomandazione:** scrivere da zero un modello AICv1 minimo (250–350 righe) per una macchina QEMU "watari-virt" senza GIC, con il timer cablato direttamente in FIQ come sul vero M1. Stima: circa 8–9 giorni [ipotesi]. Inferno non si riusa (il suo file AIC è AGPL).
- **Nota sulle regole:** l'agente ha salvato una copia del sorgente Linux `aic.c` nello scratchpad della sessione. È testo GPL pubblico e il backup era confermato, ma è contro la regola "niente download senza conferma". Segnalato all'utente.

### 2026-10-13: A9 completato (#17), il bit SIQ e le vie hardware
- File: `re/f0-timer-irq-hw.md`, con il protocollo di HW-1 (#20).
- **Verificato da Opus:**
  - `ACC_OVRD_CONVERT_SIQ_TO_IRQ` = BIT(22) di `SYS_IMP_APL_ACC_OVRD` sys_reg(3,5,15,6,0) in m1n1 `src/cpu_regs.h:541,563`;
  - nell'organizzazione AsahiLinux l'unico file che lo contiene è `src/cpu_regs.h` (ricerca nel codice), quindi non è usato da nessuna parte;
  - XNU ha `osfmk/arm/rtclock.c`.
- **Conclusioni:** macOS stesso usa il timer in FIQ (fleh_fiq → rtclock_intr) e non tocca il bit. La probabilità che HW-1 funzioni è bassa (circa 10%, stima dell'agente), ma il test costa poco e si annulla facilmente. Il watchdog resta la pista IRQ più concreta.

### 2026-10-13: A10 completato (#18), variante V6 in QEMU
- Sezione 4.4 aggiunta in `ESPERIMENTO-F0.md`.
- **Verificato da Opus:** in QEMU `hw/intc/arm_gicv3_cpuif.c` il Group 0 (`GICV3_G0`) ha un percorso dedicato (FIQ).
- **Metodo:**
  - prima prova: un driver DXE nel nostro EDK2 che mette i PPI del timer in Group 0;
  - metodo di riferimento: una patch di circa 10 righe a QEMU `virt.c` che collega l'uscita del timer direttamente a `ARM_CPU_FIQ`, come sul M1.
  Con HVF va forzato `kernel-irqchip=off` [ipotesi].
- **Esito decisivo:** se Windows si avvia e l'orologio avanza con il timer in FIQ, il problema del timer è risolto.

## Riepilogo del secondo giro
- Completati: A8, A9, A10. A7 resta bloccato (in attesa di Ciani).
- **Il quadro sul timer:**
  1. Via hardware: improbabile (HW-1, circa 10%).
  2. **Windows che gestisce la FIQ (V6): la pista più importante**, testabile in QEMU con una patch piccola.
  3. Watchdog + estensione HAL timer: concreta, ma dipende da A1/A11 (Ciani).
  4. Banco di prova senza GIC: modello AICv1 in QEMU, da scrivere da zero (circa 8–9 giorni).

## Terzo giro (branch `ralph/f0-gate-a3`, 2026-10-13)
- Marco ha riassegnato a Ralph i task di Ciani (A1, A2, A4, A11) per non bloccare la Fase 0. Ordine: A1, A2 e A4 in parallelo → A11 → A7.
- IN CORSO: A11 e A7 delegati in parallelo ad agenti Sonnet. **Non rilanciarli.**

### 2026-10-13: A1 completato (#4), estensioni HAL
- File: `re/f0-halext-doc-pubblica.md`.
- **Verdetto: "non determinabile dalle fonti pubbliche"** (circa 30% sì / 20% no / 50% incerto).
- **Verificato da Opus:** l'unico esempio pubblico di estensioni HAL (`ms-iot/imx-iotcore@public_preview hals/halext`) contiene solo `HalExtiMX6Timers`, `HalExtiMX7Timers` e `HalExtiMXDma`, cioè timer e DMA, nessun controller di interrupt.
- La specifica CSRT prevede il sottotipo "Interrupt Controller", ma non c'è nessun esempio pubblico che lo usi.
- **Nota positiva:** esistono estensioni HAL per i **timer** (i.MX), una base concreta per A11.

### 2026-10-13: A2 completato (#5), requisiti ACPI e parking protocol
- File: `re/f0-acpi-requisiti-windows.md`.
- Tabelle obbligatorie: FADT (HW_REDUCED), MADT, DSDT, DBG2. Il GICD non è dichiarato obbligatorio da Microsoft.
- Parking protocol v1 (2014): mailbox da 4 KB; risveglio con "an interrupt" non specificato; un loop WFE/polling è compatibile [ipotesi].
- **Discrepanze bloccanti in `acpigen`:** parking version/address = 0 (va messo 1 + un indirizzo); niente GICD segnaposto; la CPU di boot deve essere la prima; GICR base = 1 come nel Pi3; mancano FADT, DBG2, SPCR e DSDT.
- **Nota sulle regole:** WebFetch ha salvato il PDF della specifica nella cartella interna dei risultati dei tool di Claude, non nella repository.

### 2026-10-13: A4 completato (#7), il precedente del Raspberry Pi 3 (**risultato critico**)
- File: `re/f0-rpi3-precedente.md`.
- **Verificato da Opus:** in `tianocore/edk2-platforms` `Platform/RaspberryPi/AcpiTables/Madt.aslc:51-52` c'è il commento: "Use 1 for GICR Base Address below, since Windows 10 on Raspberry Pi 3 does not boot otherwise, and this is the value that Microsoft had in their IoT blobs."
- **Conclusione:** sul Pi 3 **nessuna terza parte** forniva il controller di interrupt. Il supporto al BCM2836 era **dentro l'HAL di Windows** (e nelle prime build ARM64 serviva perfino una patch). Il precedente quindi **non dimostra** che un'estensione HAL possa fornire un controller di interrupt.
- Il Pi 3 ARM64 avviava i core con PSCI (tramite ATF), non con il parking protocol.
