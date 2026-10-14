# ADR (bozza): adottare il piano B, Windows in dual boot sopra uno strato EL2 minimo

- Data: 2026-10-14 · Stato: **accettato da Marco** ("se la B è teoricamente possibile, adottala e vai"). Il numero si assegna al merge. Modifica ADR-0001 (che vietava l'hypervisor).

## Contesto
La Fase 0 e i primi esperimenti hanno chiuso, in pratica, la strada **nativa pura**:
- **V2 (F-14):** senza GICD nella MADT, Windows 11 ARM64 non arriva alla sessione. Il GIC sembra obbligatorio.
- **F-04, F-05:** il supporto ai controller non-GIC sta dentro l'HAL di Microsoft; l'interfaccia per fornirli tramite estensione HAL è stata rimossa.
- **V3 (F-15):** Windows usa ancora il parking protocol, ma l'avvio dei core va in deadlock e poi in bugcheck 0x5C. Dopo 6 test mirati la causa non è trovata. Il controllo con PSCI invece funziona (`docs/img/a-t1-controllo-fadt-psci-lock.png`).
- **V6 (F-13):** il timer in FIQ è inconcludente.

La ricerca `re/planB-feasibility.md` conclude che il piano B è **teoricamente possibile** (confidenza circa 85%):
- il M1 implementa in hardware l'interfaccia CPU del GICv3 virtuale (`ICH_*`); KVM di Linux la usa, con il maintenance interrupt via `AIC_VGIC_MI` (verificato in `torvalds/linux` `irq-apple-aic.c`);
- Windows funziona con un GICv3 + PSCI (V1 e controllo T1);
- i mattoni di riferimento esistono (m1n1 hv, NTASP `hv_vgic.c`/`hv_psci.c`).

## Decisione
Watari Boot diventa: **Windows ARM64 in dual boot su Apple Silicon sopra uno strato EL2 minimo** ("Watari EL2") che:
- presenta a Windows un **GICv3 virtuale**: distributor e redistributor emulati con trap di stage-2, interfaccia CPU in hardware con le list register; gli interrupt AIC delle periferiche diventano SPI virtuali;
- fornisce **PSCI** tramite HVC (accensione dei core);
- consegna il **timer** come vIRQ (la FIQ del timer si intercetta a EL2);
- traduce le SGI di Windows in IPI dell'AIC;
- lascia **tutto il resto nativo**: core a piena velocità, stage-2 identità, periferiche reali pilotate dai driver Windows.

Non c'è nessun macOS sotto: si sceglie Windows all'avvio, come Asahi. Il nostro EDK2, `acpigen` e il banco QEMU si riusano.

## Alternative scartate
- **Nativo con le funzioni interne dell'HAL** (stile NtHalExts): richiede di analizzare i binari Microsoft (escluso, F-01) ed è fragile a ogni build di Windows.
- **Continuare a cercare la strada nativa pura:** V2 indica che il GIC è obbligatorio, quindi anche risolvendo il parking il controller resterebbe bloccante.
- **Riusare NTASP:** incompleto e di bassa qualità (ADR-0007); si usa solo come riferimento.

## Conseguenze
- Il vincolo "nativo, niente hypervisor" di `CLAUDE.md` diventa "dual boot, niente macOS sotto, strato EL2 minimo consentito".
- Prototipo: sul M1 non c'è virtualizzazione annidata in HVF, quindi il primo sviluppo si fa in QEMU **TCG** con `virtualization=on`, poi sul MacBook (serve il computer di debug).
- Stima (ipotesi): circa 2–3 mesi per il prototipo in QEMU, altri 3–6 mesi per il MacBook fino alla schermata di blocco.
- Rischi principali: errata SEIS del M1 sul vGIC; fedeltà dell'emulazione GICD/GICR; codice di m1n1 pensato per il debug; licenze (KVM GPL-2.0, m1n1 MIT).
- Prossimo passo: una sessione di progettazione con `morasca-agent` sullo strato EL2.
