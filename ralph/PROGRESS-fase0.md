# Fase 0: diario di Ralph

Ralph aggiunge una voce per ogni task: data, task, file prodotti, cosa ha verificato Opus, dubbi aperti.

## Voci
### IN CORSO
- A7: aspetta i file di Ciani (A1, A2, A4).

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
