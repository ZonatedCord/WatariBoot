# A8 (issue #16): modelli QEMU di AIC esistenti e fattibilità di un banco di prova senza GIC

Data ricerca: 2026-10-13. Metodo: `gh api` in lettura (ricerca repository, albero dei file, contenuto di `hw/intc/apple_aic.c`) e lettura del driver Linux `drivers/irqchip/irq-apple-aic.c` (torvalds/linux, salvato solo in scratchpad). Nessun clone, nessun download su disco nel repo.
Convenzione: [verificato: fonte] oppure [ipotesi].

## 1. Progetti trovati

| # | Progetto | Licenza dichiarata | Stelle | Ultimo commit | Base QEMU | Rilevanza |
|---|----------|--------------------|--------|---------------|-----------|-----------|
| 1 | https://github.com/TrungNguyen1909/qemu-t8030 (iPhone 11, A13) | GitHub: NOASSERTION (repo = fork QEMU, `COPYING` GPL-2.0, 17992 byte) | 2204 | 2022-10-22, archiviato | 7.1.0 (`VERSION.QEMU`) | alta: modello AIC pulito, 683 righe |
| 2 | https://github.com/ChefKissInc/Inferno (continuazione di #1, "QEMUAppleSilicon") | `COPYING` GPL-2.0 (17880 byte), ma **il file AIC ha header AGPL-3.0-or-later** | 3955 | 2026-07-29 (repo pushed 2026-09-20) | 10.2.2 / 10.1.3 "hybrid" (tag `ChefKissInferno-1.0.0rc1_QEMU-10.2.2_10.1.3_hybrid`) | alta per aggiornamento API, 636 righe |
| 3 | https://github.com/jprx/qemu-sptm (macchina `-M darwin`, sottomodulo di `darwin-vm`) | NOASSERTION (QEMU GPL-2.0) | 52 | 2026-09-26 | 11.1.0 (`VERSION`) | media: AIC "finto", 280 righe |
| 4 | https://github.com/alephsecurity/xnu-qemu-arm64 (iOS 12, s8000) | NOASSERTION | 1459 | 2021-09-16 | circa 4.x [ipotesi] | bassa: non letto, nessun AIC modellato per quanto so [ipotesi] |
| 5 | https://github.com/imbushuo/qemu (patch HVF per host Apple Silicon) | NOASSERTION | 44 | 2026-09-27 | n/d | nessuna: non emula un SoC Apple, usa solo l'host M1 |

Tutti i dati di tabella: [verificato: `gh api repos/...` e `.../git/trees`]. Le righe sono quelle del file `hw/intc/apple_aic.c` [verificato: `wc -l` sul contenuto].
Cercati senza risultati: query GitHub "qemu apple aic", "qemu t8030 aic", "apple aic qemu hw/intc" (0 risultati); nessun fork di Asahi per test con AIC emulato trovato [verificato: ricerche vuote]; non ho cercato Corellium open (non ha fonti pubbliche note, [ipotesi]).

## 2. Cosa fanno i tre modelli AIC

### 2.1 qemu-t8030 (`hw/intc/apple_aic.c`, 683 righe, header 1033 byte)
- Layout registri = **AIC "v1" come in Linux** [verificato: confronto con `irq-apple-aic.c`]: REV 0x0000, CAP0 0x0004, RST 0x000C, GLB_CFG 0x0010, WHOAMI 0x2000, IACK/EVENT 0x2004, IPI_SET 0x2008, IPI_CLR 0x200C, IPI_MASK_SET/CLR 0x2024/0x2028, DEFER_SET/CLR 0x202C/0x2030, EIR_DEST (target cpu) 0x3000, SW_SET 0x4000, SW_CLR 0x4080, MASK_SET 0x4100, MASK_CLR 0x4180, INT_RO 0x4200, alias per CPU a 0x5000 + n*0x80. In Linux il sw_set di AICv1 cade a 0x3000 + 4*0x400 = 0x4000 [verificato: aic.c righe ~1000-1033, calcolo mio].
- Il codice dice `AIC_VERSION 2` e `REV` restituisce 2, ma la mappa è quella v1 di Linux. Non è AICv2 (che ha IRQ_CFG a 0x2000, mask a 0x6400 ecc. nel modello jprx) [verificato: codice].
- IRQ: stato per linea (set_irq da gpio in), mascheramento via MASK_SET/CLR, destinazione per CPU via EIR_DEST, SW_SET/SW_CLR, INT_RO. Un'interruzione consegnata con IACK viene mascherata (comportamento come hardware, auto-mask). Codifica evento IACK: EXT = 0x10000 | num, IPI = 0x40001 (altri) / 0x40002 (self), 0 = spurio. Coincide con `AIC_EVENT_TYPE_IRQ=1`, `TYPE_IPI=4`, `IPI_OTHER=1`, `IPI_SELF=2` di Linux [verificato].
- IPI legacy: SET/CLR/MASK/DEFER implementati [verificato: codice]. Le "fast IPI" (sysreg `IPI_RR_*`) NON stanno nell'AIC: sono nel modello CPU `apple_a13.c`, che espone un gpio `fast_ipi` verso FIQ [verificato: `apple_a13.c` realize].
- Timer ARM -> FIQ: **non nell'AIC**. Nel modello CPU un `TYPE_OR_IRQ` a 16 linee è collegato a `ARM_CPU_FIQ`; `GTIMER_VIRT` va alla linea 0 e `fast_ipi` alla linea 1 [verificato: `apple_a13.c`, ~righe 564-588]. Questo è l'esatto schema che ci serve per il timer.
- Costi/limiti: numero CPU/IRQ ricavato dal device tree Apple (`ipid-mask`, `#shared-timestamps`, nodo `aic-timebase`) tramite `hw/arm/xnu_dtb.h` (API specifiche del fork); thread `QEMUTimer` ogni 64 us (`kAICWT`) che richiama update (polling); `msi_nonbroken = true` (hack); `#define AIC_CPU_COUNT 6` e T8030 codificati a mano; offset timebase 0x20/0x28 (contatore 24 MHz emulato) nel registro AIC [verificato: codice].
- Licenza: file senza header; contribuito da Trung Nguyen in un fork di QEMU 7.1 (GPL-2.0). Va trattato come GPL-2.0 [ipotesi, da controllare con autore se si vuole copia testuale].

### 2.2 Inferno (`hw/intc/apple_aic.c`, 636 righe)
- Stesso modello di #1 portato a QEMU 10.x: stesso layout registri, stessi codici evento, stessa struttura. Cambiano nomi (`REG_AIC_*`), `set_bit32`, API DT (`hw/arm/apple-silicon/dt.h`, `AppleDTNode`) [verificato: header e codice].
- **Licenza del file: AGPL-3.0-or-later** (copyright 2024-2026 VisualEhrmanntraut e Christian Inci) [verificato: header del file]. Incorporarlo in un binario QEMU modificato è problematico: AGPL §13 impone l'offerta del sorgente anche per uso via rete, e la combinazione con QEMU GPL-2.0-only va verificata. Non copiare senza decisione esplicita.
- È la fonte migliore per vedere come si adatta l'API QEMU 10.x, ma non per il riuso diretto.

### 2.3 qemu-sptm / darwin-vm (`hw/intc/apple_aic.c`, 280 righe)
- Dichiaratamente "minimal": legge `compatible` "aic,N" e supporta v1, v2, v3; conosce il numero di IRQ, abilita/disabilita, ack, e **spedisce tutto a un'unica linea verso il core 0** [verificato: commento in testa e codice].
- Per v1: IACK 0x2004, num ints 0x4, disable 0x4100, enable 0x4180, ack 0x4080 (nota: la loro denominazione è invertita rispetto a Linux dove 0x4100 è MASK_SET) [verificato: confronto con aic.c; suggerisce che usano "ENABLE=clear mask"; coerente].
- **Niente IPI, niente DEST per CPU, niente SW_SET**, multicore non funziona; il README dichiara FIQ/UART ma l'AIC non gestisce il timer [verificato: README e codice].
- API aggiornate a QEMU 11.1: `hw/core/sysbus.h`, `hw/core/irq.h` (include spostati nel nuovo albero), `OBJECT_DECLARE_SIMPLE_TYPE`, `class_init(..., const void *data)` [verificato: codice]. Utile come riferimento di API correnti.
- Licenza non esplicita nel file [verificato: nessun header]; repo fork QEMU GPL-2.0.

## 3. Innesto su `virt` aarch64 o su una macchina "watari-virt"

Cosa serve in QEMU (API stabili, indipendenti dai fork) [ipotesi, basata sul codice letto]:
1. Nuovo dispositivo sysbus `watari-aic` (modello da #1/#3): `qdev_init_gpio_in(n_irq)`, un `sysbus_init_irq` per CPU, regione MMIO.
2. Macchina nuova `hw/arm/watari-virt.c` (copia ridotta di `virt.c` senza `create_gic()`): CPU, RAM, PL011/UART, flash/UEFI, PCIe ecam, virtio-mmio. Le linee degli IRQ dei dispositivi (`virt` usa SPI del GIC: UART, PCIe INTx, virtio) vanno collegate a `qdev_get_gpio_in(aic, n)`.
3. Per ogni CPU: `sysbus_connect_irq(aic, cpu, qdev_get_gpio_in(cpu, ARM_CPU_IRQ))` come in `t8030.c` (~righe 768-772) [verificato].
4. Timer: collegare i `GTIMER_*` uscenti della CPU a `ARM_CPU_FIQ` (tramite `TYPE_OR_IRQ` come in `apple_a13.c`); sulla `virt` standard sono invece PPI del GIC. Questa è la parte che rende il banco un'imitazione fedele dell'M1, dove il timer arriva come FIQ e non via AIC [verificato per t8030; per M1: Linux crea FIQ AIC_TMR_EL0_PHYS/VIRT dentro l'AIC come domain separato, aic.c ~righe 552-600].
5. Fast IPI di M1 (t8103 usa `aic1_local_fipi_info` in Linux, aic.c ~righe 308-312 [verificato]): richiedono sysreg IMP-DEF nella CPU (`IPI_RR_*`, `IPI_SR_EL1`, ecc.) che QEMU TCG non ha. Sulla `watari-virt` con CPU `max`/`cortex-a72` queste non esistono; per i nostri test è realistico usare solo IPI legacy (come l'AIC con `compatible` "apple,aic") [verificato: Linux supporta `apple,aic` = nessun fast IPI].
6. Non serve il device tree Apple (ADT): si fornisce info come proprietà del dispositivo (`num-irq`, `num-cpu`) invece di leggere `ipid-mask`, così si elimina la dipendenza `xnu_dtb.h`.

Dipendenze di #1 da rimuovere: `hw/arm/xnu_dtb.h`, `msi_nonbroken`, il timer di polling a 64 us (sostituibile con calcolo immediato in `set_irq`), il timebase nel registro. API da aggiornare rispetto a QEMU attuale: `VMSTATE_*` array, `DEVICE_NATIVE_ENDIAN`, include `hw/core/*`, `class_init(const void *)`, `qemu_mutex_unlock` dentro `WITH_QEMU_LOCK_GUARD` (da riscrivere) [ipotesi].

Sforzo: portare #1 a una `watari-virt` su QEMU recente: 1-2 giorni per il dispositivo + 2-3 per la macchina e il cablaggio dei dispositivi.

## 4. Raccomandazione

**Scrivere da zero un modello AICv1 minimo (circa 250-350 righe), usando #1 (qemu-t8030, GPL-2.0, 683 righe) come riferimento di struttura e il driver Linux come specifica dei registri, e #3 (qemu-sptm) come riferimento delle API QEMU attuali.**
- Perché non riusare un progetto intero: Inferno/t8030 portano un'intera macchina iPhone (ADT, SEP, DART, ecc.) irrilevante per noi; Inferno ha AGPL sul file AIC; t8030 è su QEMU 7.1 e archiviato.
- Perché non estrarre il file così com'è: dipende dal DT Apple, ha hack (`msi_nonbroken`, polling) e hardcode T8030; la pulizia costa quanto riscriverlo, e la licenza di #1 è implicita.
- Se in futuro si preferisce copiare, usare solo #1 con header di provenienza GPL-2.0 e chiedere conferma all'autore [ipotesi su licenza].

Stima in giorni [ipotesi]:
| Attività | Giorni |
|---|---|
| Dispositivo AICv1 (IRQ, mask, dest, SW_SET/CLR, IACK, IPI legacy), test con driver Linux `irq-apple-aic` | 2 |
| Macchina `watari-virt` (partenza da `virt.c` senza GIC, UART, PCIe, virtio, cablaggio IRQ ad AIC) | 3 |
| Timer ARM -> FIQ (OR gate sulla CPU) e test | 1 |
| Test bring-up con UEFI/Windows ARM64 + debug | 2-3 |
| Totale | circa 8-9 |

## 5. Minimo che il modello deve fare per i nostri test

Specificato dai driver m1n1/Linux (`irq-apple-aic.c`):
1. Info: lettura `0x0004` con `NR_IRQ` nei 16 bit bassi (Linux `AIC_INFO_NR_IRQ`); `0x2000 WHOAMI` = id CPU.
2. Maschere: `MASK_SET 0x4100 + 4*(n/32)`, `MASK_CLR 0x4180 + 4*(n/32)`, tutte mascherate dopo reset; lettura di `MASK_*` ritorna lo stato.
3. Target: `TARGET_CPU 0x3000 + 4*n` (Linux scrive bitmask CPU); default CPU0.
4. Sorgenti software: `SW_SET 0x4000`, `SW_CLR 0x4080` (usati per test e per `irq_set_irqchip_state`).
5. `EVENT 0x2004`: ritorna `(1<<16)|n` per IRQ esterna pendente, non mascherata e destinata a quella CPU; 0 quando non ce ne sono (Linux cicla finché `event != 0`); l'IRQ consegnata resta mascherata finché `MASK_CLR` (eoi) [verificato: comportamento in Linux `aic_handle_irq` e `aic_irq_eoi`].
6. IPI legacy: `IPI_SEND 0x2008` (bit per CPU e `BIT(31)` self), `IPI_ACK 0x200C`, `IPI_MASK_SET/CLR 0x2024/0x2028`; evento `(4<<16)|1` (other) o `(4<<16)|2` (self) [verificato: aic.c righe 82-96].
7. Uscita: una linea IRQ per CPU verso `ARM_CPU_IRQ`, alzata se esiste almeno una causa non mascherata, abbassata dopo che l'evento è consumato.
8. Timer: nessun registro AIC; i timer ARM (fisico e virtuale EL1/EL0) vanno cablati a `ARM_CPU_FIQ` nella macchina, con un FIQ mascherabile come in M1. Se ci serve anche il "FIQ via AIC" come domain separato (Linux `AIC_TMR_*`, `VM_TMR_FIQ_ENA_EL2`), servono sysreg Apple (`SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2`) nella CPU: fuori dal minimo, da valutare con F0 FIQ (`re/f0-m1-fiq-irq.md`) [ipotesi].
9. Non servono per il minimo: DEFER IPI, GLB_CFG, timestamp, fast IPI, PMU FIQ, multi-die, AICv2/v3.

## 6. Cose non verificate / da fare

- Non ho letto il driver m1n1 (`src/aic.c`) direttamente: la specifica dei registri è dedotta da Linux e dal modello t8030 [ipotesi per m1n1].
- Licenza effettiva del file AIC di qemu-t8030 non dichiarata nel file [ipotesi: GPL-2.0].
- Da verificare se QEMU 10.x/11.x ha già `hw/arm/virt` con opzione `gic-version=none` o simili: per la mia ricerca il QEMU ufficiale non permette di togliere il GIC [dato dalla consegna dell'utente, non riverificato].
