# F0 / A3: sorgenti FIQ del t8103 (M1) e alternative in IRQ

Task A3 di Ralph, branch `ralph/f0-gate-a`. Analisi basata solo sul codice e sulla documentazione di Asahi, letti in sola lettura con `gh api`. Nessun binario o simbolo Microsoft è stato usato.

## 0. Convenzioni e fonti

Ogni affermazione è marcata:
- **[V: fonte:righe]** = verificato leggendo quel file (numeri di riga dall'output di `cat -n` sul riferimento indicato).
- **[I]** = ipotesi o deduzione, da verificare.

Sigle di riferimento:

| Sigla | Repo e riferimento |
|---|---|
| `L` | AsahiLinux/linux, branch di default `asahi`, HEAD `77cb8f24c2381a8abb7272d7bbdec548d6426a8a` |
| `M` | AsahiLinux/m1n1, branch di default `main`, HEAD `fbec52a81f2b12a5cf75f83a653d46df0bbc36ce` |
| `D` | AsahiLinux/docs, `main`, HEAD `0d1f8917fa88745d62a4c05d802c4a7298c8616b` (testo di asahilinux.org/docs) |
| `L0` | AsahiLinux/linux, commit `76cde26394` (8 aprile 2021, primo commit di `irq-apple-aic.c`) |

File principali letti: `L:drivers/irqchip/irq-apple-aic.c`, `L:arch/arm64/include/asm/apple_m1_pmu.h`, `L:drivers/perf/apple_m1_cpu_pmu.c` (solo i punti IRQ), `L:drivers/clocksource/arm_arch_timer.c` (nessuna gestione Apple), `L:arch/arm64/boot/dts/apple/t8103.dtsi`, `L:include/dt-bindings/interrupt-controller/apple-aic.h`, `L:Documentation/devicetree/bindings/interrupt-controller/apple,aic.yaml`, `L:drivers/watchdog/apple_wdt.c`, `M:src/aic.c`, `M:src/aic_regs.h`, `M:src/exception.c`, `M:src/smp.c`, `M:src/utils.c`, `M:src/cpu_regs.h`, `M:src/chickens.c`, `M:src/wdt.c`, `M:src/hv/hv_exc.c`, `M:src/hv/hv.c`, `D:docs/hw/soc/aic.md`, `D:docs/hw/soc/wdt.md`, `D:docs/hw/cpu/system-registers.md`.

Contesto del t8103 nel DT Linux:
- Compatibile AIC: `"apple,t8103-aic","apple,aic"` **[V: L:t8103.dtsi:1029-1030]**.
- Nel driver `apple,t8103-aic` mappa su `aic1_local_fipi_info` (AIC v1 con Fast IPI locali) **[V: L:irq-apple-aic.c:281-289,309-313]**.
- Il nodo `timer` e i nodi PMU sono tutti agganciati ad `AIC_FIQ`, non ad `AIC_IRQ` **[V: L:t8103.dtsi:368-388]**.
- Il binding conferma che "this device also represents the FIQ interrupt sources ... which do not go through a discrete interrupt controller" **[V: L:apple,aic.yaml, descrizione]**. Il documento Asahi dice lo stesso: il timer di sistema "bypasses AIC. It is wired straight to FIQ" **[V: D:aic.md, sezione Timer]**.

---

## 1. Elenco delle sorgenti FIQ del t8103

Il gestore FIQ di Linux (`aic_handle_fiq`) ammette di non avere un registro di stato globale. Controlla una per una le sorgenti, perché "not dealing with any of these results in a FIQ storm" **[V: L:irq-apple-aic.c:557-572]**. L'elenco completo delle sorgenti che Linux controlla è quindi il seguente.

| # | Sorgente | Come la riconosce Linux (`aic_handle_fiq`) | Registri che la controllano | Mascherabile alla fonte? |
|---|---|---|---|---|
| 1 | **Fast IPI** (solo se `use_fast_ipi`, vero sul t8103) | `IPI_SR_EL1 & IPI_SR_PENDING` **[V: L:574-576]** | `SYS_IMP_APL_IPI_RR_LOCAL_EL1` s3_5_c15_c0_0 e `..._GLOBAL_EL1` s3_5_c15_c0_1 per inviare; `IPI_SR_EL1` s3_5_c15_c1_1 (bit 0 PENDING, write 1 to clear); `IPI_CR_EL1` s3_5_c15_c3_1 (countdown deferred) **[V: L:171-192]** | **Sì.** Solo se nessuno la invia: nessuna scrittura a `IPI_RR_*`. Si azzera lo stato pendente con `IPI_SR_EL1 = PENDING` **[V: L:aic_init_cpu 864-865]**. **Non esiste un bit di mask** nei sorgenti letti [I]. |
| 2 | **Timer fisico EL0** (`CNTP_*_EL0`, indicato come "HV phys", hwirq 0) | `TIMER_FIRING(cntp_ctl_el0)` = ENABLE=1, IMASK=0, ISTATUS=1 **[V: L:552-555,578-580]** | `CNTP_CTL_EL0` (ENABLE, IMASK, ISTATUS), `CNTP_CVAL/TVAL_EL0` | **Sì, architetturale:** `CNTP_CTL_EL0.IMASK=1` (o ENABLE=0). Linux lo fa a ogni CPU in `aic_init_cpu` **[V: L:868]**. m1n1 scrive 7 (ENABLE|IMASK|ISTATUS) in `exception_initialize` **[V: M:exception.c:134]** |
| 3 | **Timer virtuale EL0** (`CNTV_*_EL0`, "HV virt", hwirq 1) | `TIMER_FIRING(cntv_ctl_el0)` **[V: L:582-584]** | `CNTV_CTL_EL0` ecc. | **Sì**, `CNTV_CTL_EL0.IMASK=1` **[V: L:869; M:exception.c:135]** |
| 4 | **Timer fisico "guest"** (`CNTP_CTL_EL02`, hwirq 2; esiste solo con kernel a EL2/VHE) | `VM_TMR_FIQ_ENA_EL2.P` e `TIMER_FIRING(CNTP_CTL_EL02)` **[V: L:586-592]** | `SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2` s3_5_c15_c1_3 (bit 0 = V, bit 1 = P) **[V: L:187-189]**; `CNTP_CTL_EL02` | **Sì, con due leve:** il bit di enable IMP-DEF (è "l'unico mask vero" secondo il commento di Linux: "Only the guest timers have real mask bits" **[V: L:502]**) e `IMASK` architetturale |
| 5 | **Timer virtuale "guest"** (`CNTV_CTL_EL02`, hwirq 3) | come sopra, bit V **[V: L:594-597]** | come sopra | **Sì**, come il punto 4 |
| 6 | **PMU core** (E-core ed P-core) | `(PMCR0 & (IMODE\|IACT)) == (IMODE_FIQ\|IACT)` **[V: L:600-603]** | `SYS_IMP_APL_PMCR0_EL1` s3_1_c15_c0_0: IMODE [10:8] (0 off, 1 PMI, 2 AIC, 3 HALT, 4 FIQ), IACT bit 11, PMI enable [19:12] e [45:44] **[V: L:apple_m1_pmu.h:22-37]** | **Sì:** `PMCR0.IMODE=OFF` e IACT=0, come in `aic_init_cpu` **[V: L:882-883]**. Linux imposta IMODE_FIQ solo quando abilita il PMU **[V: L:apple_m1_cpu_pmu.c:538]** |
| 7 | **PMC uncore** | `UPMCR0.IMODE==FIQ` e `UPMSR.IACT` **[V: L:605-607]**; il gestore non la serve, solo la spegne ("Uncore PMC FIQ fired. Masking.") **[V: L:609-611]** | `SYS_IMP_APL_UPMCR0_EL1` s3_7_c15_c0_4 (IMODE [18:16] su T8020/M1: 0 off, 2 AIC, 3 HALT, 4 FIQ) e `UPMSR_EL1` s3_7_c15_c6_4 (bit 0 IACT) **[V: L:195-204]** | **Sì:** `UPMCR0.IMODE=OFF` **[V: L:886-889]**. Nota: su A11 la posizione del campo IMODE cambia (T8015: [10:8]) **[V: M:cpu_regs.h:609-611]** |
| 8 | vGIC maintenance IRQ | Non è un FIQ fisico: è un "FIQ finto" (`AIC_VGIC_MI`) generato quando c'è KVM a EL2 **[V: L:428-441,1102-1115]** | `ICH_HCR_EL2`, `ICH_MISR_EL2` | Irrilevante senza hypervisor; Linux lo spegne con `ICH_HCR_EL2.En=0` **[V: L:878]** |

Altre note:
- Il numero e il significato delle sorgenti sono quelli del binding: 0 HV phys, 1 HV virt, 2 guest phys, 3 guest virt, 4 PMU E, 5 PMU P **[V: L:apple-aic.h; L:apple,aic.yaml]**.
- Nel kernel Linux a **EL1** (non VHE) i timer "guest" diventano i timer EL0 normali (hwirq 2/3 rimappati su 0/1), mentre quelli "HV" restituiscono `-ENOENT` **[V: L:aic_irq_domain_translate 718-736]**. Se Windows gira a EL1 usa quindi i timer `CNTP/CNTV_*_EL0`; se si servisse di EL2 (es. Hyper-V/VBS) toccherebbe anche i timer EL02 e il registro `VM_TMR_FIQ_ENA_EL2` [I].
- PMU E e PMU P sono due FIQ distinte ma Linux le fonde in una **[V: L:738-740]**.
- Non esiste un registro unico che dica "quale FIQ è attiva" **[V: L:559-562, commento]**.
- La stessa lista di sorgenti FIQ compare nel gestore diagnostico di m1n1 `exc_fiq`: timer EL0, timer EL02, PMCR0, `IPI_SR_EL1`, UPMCR0/UPMSR **[V: M:exception.c:395-461]**. Un'altra implementazione indipendente arriva alla stessa lista.
- Altri campi o registri **non documentati** che potrebbero generare FIQ: `UPMSR_EL1` bit 1 ("CTI") **[V: D:system-registers.md, sezione UPMSR]**. Non c'è alcun codice che lo gestisca. Non sappiamo se produca un FIQ [I].

### Registri PMU/IPI: discrepanze tra le fonti
- `IPI_RR_*`: Linux definisce CPU = [7:0] e cluster = [23:16] **[V: L:173-175]**. La documentazione Asahi scrive CPU = [3:0] e cluster = [20:16] **[V: D:system-registers.md, sezione IPI]**. Il tipo [29:28] è concorde (0 immediate, 1 retract, 2 deferred, 3 nowake). Per il t8103 vale Linux (è codice funzionante), ma il nostro codice deve usare l'accesso esatto di Linux [I].
- m1n1 e Linux hanno numerazioni diverse dei campi di `UPMCR0` per A11, uguali per M1 **[V: M:cpu_regs.h:602-619]**.

---

## 2. Il timer architetturale si può instradare in IRQ?

**Risposta onesta: nelle fonti Asahi non esiste alcun modo documentato per farlo arrivare come IRQ.** Prove:

1. Il DT mappa i timer solo come `AIC_FIQ` **[V: L:t8103.dtsi:368-376]**; il driver li tratta come FIQ "hard-wired" che non attraversano l'AIC **[V: L:apple,aic.yaml; D:aic.md]**.
2. La documentazione AIC dice: "The system timer is the standard ARM64 MSR stuff, and bypasses AIC. It is wired straight to FIQ." **[V: D:docs/hw/soc/aic.md, fine file]**.
3. `arm_arch_timer.c` non ha alcuna gestione specifica Apple. Cerca "apple" e "fiq": nessuna corrispondenza **[V: L:drivers/clocksource/arm_arch_timer.c, grep]**. Il timer viene usato così com'è, e il lavoro sporco sta tutto nell'irqchip.
4. Linux non fa nessun "vtimer tramite FIQ e mascheramento IMP-DEF" tipo oggetto di remap. Cosa fa in realtà:
   - Per i timer EL0 nel percorso FIQ usa `irq_ack = aic_fiq_set_mask` e `irq_eoi = aic_fiq_eoi`; ma `aic_fiq_set_mask` agisce **solo sui timer guest** (EL02) tramite `VM_TMR_FIQ_ENA_EL2`; per gli altri casi è un no-op **[V: L:500-531, 620-627]**.
   - Come "ack" dei timer EL0 serve il mascheramento architetturale: il generico `arch_timer_handler` imposta `IMASK` nel proprio gestore (codice generico del driver ARM). Quindi il livello FIQ si abbassa perché la sorgente (ISTATUS+!IMASK) smette di asserire [I, deduzione dal commento "We mask to ack (where we can)" **[V: L:547]**].
5. Possibili piste, tutte **non verificate**:
   - **(a)** `ACC_OVRD_CONVERT_SIQ_TO_IRQ` (bit 22 di `SYS_IMP_APL_ACC_OVRD` s3_5_c15_c6_0) è definito in `M:cpu_regs.h:563` ma **non è mai usato** in nessun repo dell'organizzazione AsahiLinux (ricerca `CONVERT_SIQ_TO_IRQ` nell'organizzazione: un solo risultato, la definizione). Il nome suggerisce una conversione "SIQ → IRQ", ma non sappiamo cosa sia "SIQ" né se riguardi il timer o i FIQ [I, pista di ricerca da testare su hardware, a rischio].
   - **(b)** `CYC_OVRD` bit [21:20] "FIQ mode (2=disable)" e [23:22] "IRQ mode (2=disable)" **[V: D:system-registers.md; M:cpu_regs.h:530-536]**. m1n1 li usa solo in `cpu_sleep()` (sleep non profondo) per fare in modo che un CPU in WFI non venga svegliato da IRQ/FIQ **[V: M:utils.c:267-271]**. Non c'è evidenza che rimappino FIQ su IRQ: sembrano controlli sul **risveglio da WFI**, non sul tipo di segnale [I].
   - **(c)** `PMCR0.IMODE`/`UPMCR0.IMODE` valore 2 = "AIC" **[V: D:system-registers.md; L:apple_m1_pmu.h:27; L:195-200]**: per la PMU esiste una modalità in cui l'interrupt va all'AIC (quindi come IRQ). Linux non la usa e il DT t8103 non indica quale hwirq AIC sarebbe. Vale solo per PMU, non per il timer [I].
   - **(d)** Un guscio a EL2 (come `hv_update_fiq` di m1n1) può trasformare i FIQ in FIQ/IRQ virtuali con `HCR_EL2.VF` **[V: M:hv_exc.c:152-180]**. Questo è per definizione un hypervisor, quindi contrario al vincolo "niente hypervisor". Va tenuto solo come piano B [I].
6. Conclusione operativa: **il timer architetturale (CNTP/CNTV) va considerato solo-FIQ.** Per far arrivare a Windows un tick in IRQ bisogna evitare di abilitarlo (IMASK=1 / ENABLE=0) e dare a Windows un'altra sorgente di tick (sezione 3), oppure scoprire sperimentalmente la pista (a).

Nota: lettura di `CNTPCT_EL0`/`CNTVCT_EL0` (il contatore) non genera interrupt e non è toccata dal mascheramento; ci sarà sempre un contatore fisico a 24 MHz [I, comune ARMv8; l'AIC espone anche una copia del contatore a 0x8020/0x8028 **[V: D:aic.md]**, ma questo intervallo supera la finestra 0x8000 del nodo DT, quindi va verificato prima di usarlo].

---

## 3. Timer MMIO alternativi che generano IRQ AIC

### 3.1 Cosa c'è nel DT del t8103
Ho elencato tutti i `compatible` di `t8103.dtsi` **[V: L:t8103.dtsi, elenco completo con grep]**. Gli unici blocchi con "timer" o conteggio del tempo sono:
- `arm,armv8-timer`: FIQ (sezione 2).
- **`apple,t8103-wdt`**: watchdog con interrupt `AIC_IRQ 338`, livello alto, clock `clkref` = 24 MHz **[V: L:t8103.dtsi:1223-1229, 390-396]**.
- `apple,t8103-smc` con sotto-nodo `smc-rtc`: RTC via SMC (mailbox/protocollo RTKit), non è un timer programmabile a bassa latenza [I; non analizzato].
- `apple,t8103-fpwm`, `apple,t8103-nco`: PWM e generatore di clock, non timer con IRQ **[V: solo compatible]**.

Non ho trovato nel DT di Asahi per t8103 nessun altro timer MMIO "di sistema" (nessun `arm,armv7-timer-mem`, nessun timer generico) **[V: elenco dei compatible]**. L'assenza nel DT non prova che l'hardware non ne abbia: il DT contiene solo ciò che Linux usa [I].

### 3.2 Il watchdog (WDT) come candidato
Registri (MMIO base 0x23d2b0000, dimensione 0x4000) **[V: L:apple_wdt.c:19-52]**:

| Offset | Registro |
|---|---|
| 0x00 / 0x04 / 0x08 / 0x0c | WD0: CUR_TIME / BITE_TIME / BARK_TIME / CTRL |
| 0x10 / 0x14 / 0x1c | WD1: CUR_TIME / BITE_TIME / CTRL |
| 0x20 / 0x24 / 0x2c | WD2: CUR_TIME / BITE_TIME / CTRL |

- CTRL: bit 0 IRQ_EN, bit 1 IRQ_STATUS (write 1 to clear), bit 2 RESET_EN **[V: apple_wdt.c:50-52]**.
- CUR_TIME incrementa a ogni tick di clkref (24 MHz) ed è scrivibile **[V: apple_wdt.c:27-28]**.
- Quando `RESET_EN` è attivo e `CUR >= BITE`, la macchina si resetta; WD1 e WD2 fanno un reset normale, **WD0 un reset in recovery mode** **[V: apple_wdt.c:22-26; D:wdt.md]**.
- Il commento del driver afferma: "WD0 additionally supports a configurable interrupt" e che con IRQ_EN e `CUR >= BARK` scatta un interrupt, ripulito scrivendo 1 in IRQ_STATUS **[V: apple_wdt.c:22-35]**. Il commento però cita nomi di WD1 (`APPLE_WDTx_WD1_CUR_TIME`/`BARK_TIME`) mentre il registro BARK esiste solo per WD0 (offset 0x08): probabilmente un errore di battitura nel commento [I].
- Il DT dichiara un solo IRQ AIC (**338**) per tutto il blocco **[V: t8103.dtsi:1228]**.
- Il driver Linux **non richiede mai questo IRQ** (nessun `irq` nel probe, solo start/stop/ping/restart) **[V: apple_wdt.c:56-139, grep di `irq|bark|WD0` nel probe]**. Quindi la funzionalità "bark" non è stata mai collaudata in Linux; solo il commento la descrive. [I: funziona, ma non c'è evidenza]
- Stato iniziale: "The initial macho (usually m1n1) is booted with the watchdog timer enabled", compare iniziale 120 s; i contatori sono a 32 bit e "wrap", quindi il massimo timeout è poco meno di tre minuti **[V: D:wdt.md]**. m1n1 lo disattiva scrivendo 0 in `+0x1c` (WD1) all'avvio e, per `wdt-version` 2 o 3 (M2 e successivi), anche il secondario **[V: M:wdt.c:14-48]**. Su t8103 non vedo che m1n1 tocchi WD0/WD2 [I: stato iniziale di WD0 e WD2 sconosciuto].

### 3.3 Valutazione come timer di sistema per Windows (via estensione HAL timer)
Pro:
- Genera un vero **IRQ AIC** (hwirq 338, livello), con affinità programmabile via `TARGET_CPU` **[V: L:aic_irq_set_affinity 445-463]**.
- Clock a 24 MHz (risoluzione ~41,7 ns), contatore a 32 bit scrivibile **[V]**.
- Il blocco ha **tre** comparatori (WD0/1/2), ma solo WD0 ha il BARK con interrupt (secondo il commento) **[V/I]**.

Contro e rischi:
1. L'interrupt è documentato solo da un commento, mai usato in Linux. Non sappiamo se sia sempre abilitato in hardware o se serva sbloccare qualcosa [I].
2. Il confronto è `CUR >= BARK`, non modulare. Il contatore a 32 bit fa il giro in circa 179 s. Per programmare un tick bisogna azzerare `CUR_TIME` e impostare `BARK`; ogni riprogrammazione è un paio di scritture MMIO non in ordine con il contatore (la latenza MMIO non è quantificata nelle fonti) [I].
3. Con WD0 se `RESET_EN` è attivo e `BITE` scade, la macchina si riavvia **in recovery mode** **[V]**. Bisogna garantire `RESET_EN=0` su WD0 e impostare `BITE` al massimo, altrimenti un bug nella HAL riavvia il Mac. Pericoloso e va provato solo con backup e sotto supervisione.
4. È un **solo** timer per tutto il SoC, non uno per CPU. Windows ARM64 si aspetta di norma un timer per processore [I]; servirebbe multiplexare con IPI, il che dipende dalla sezione 4.
5. Il blocco `wdt` si trova in un dominio always-on ("pmgr_mini" vicino) e sopravvive ai reset parziali, quindi è condiviso con m1n1/macOS/recovery: modificare le sue impostazioni a livello persistente è rischioso per la sicurezza dell'hardware (vedi vincolo del progetto) [I].
6. Windows richiede anche un contatore di prestazioni monotono (QPC). Quel contatore resta `CNTPCT_EL0`, che è solo da leggere e non genera FIQ se il timer è mascherato [I].

Conclusione sezione 3: **il WDT è l'unico candidato visibile nel DT** per un tick via IRQ, ma è un candidato **di ripiego, non verificato**. Prima di basarci su di esso serve un esperimento su hardware (con backup confermato): programmare WD0 con RESET_EN=0, BARK a pochi ms, e vedere se arriva AIC IRQ 338. Gli ADT dell'hardware (non consultati) potrebbero elencare altri timer [I].

### 3.4 Altra leva: SW_SET dell'AIC
L'AIC permette di asserire a software un qualsiasi hwirq: "Software triggering (ORed with hw line)" **[V: L:irq-apple-aic.c:18; D:aic.md]**; m1n1 espone `aic_set_sw()` con SW_SET a 0x4000 e SW_CLR a 0x4080 **[V: M:aic.c:192-202; aic_regs.h:12-13]**. Non è un timer, ma permette di creare "IPI virtuali" o di rimandare lavoro come IRQ (sezione 4) [I].

---

## 4. IPI "legacy" dell'AIC in IRQ

### 4.1 Come funzionano (AIC v1, quindi t8103)
Registri MMIO (vista "this CPU", base AIC `0x23b100000`) **[V: L:irq-apple-aic.c:76-103; M:aic_regs.h:5-20; D:aic.md]**:
- `0x2000 WHOAMI`, `0x2004 EVENT` (lettura = acknowledge + motivo), `0x2008 IPI_SEND`, `0x200c IPI_ACK`, `0x2024 IPI_MASK_SET`, `0x2028 IPI_MASK_CLR`.
- `IPI_SEND`: bit `0..30` = invia l'IPI "other" alla CPU con quell'indice; bit 31 = IPI "self" alla CPU corrente **[V: D:aic.md; L:93-96]**.
- `EVENT` restituisce tipo (bit 23:16) e numero (15:0): tipo 4 = IPI, numero 1 = OTHER, 2 = SELF **[V: L:79-86]**.
- Viste per-CPU esplicite: `AIC_CPU_IPI_SET/CLR/MASK_SET/MASK_CLR(cpu)` a `0x5008 + (cpu<<7)` ecc.; **definite ma mai usate** da Linux e m1n1 **[V: L:100-103; M:aic_regs.h:17-20]**; la documentazione dice "TODO Document direct access to per-core state offsets" **[V: D:aic.md]**.

Flusso (documentato Asahi) **[V: D:aic.md, "IPI flow"]**:
1. Scrivere il bit in `IPI_SEND`: la linea IRQ ARM viene asserita.
2. Leggere `EVENT`: l'IPI viene mascherata e la linea si abbassa.
3. Scrivere il bit in `IPI_ACK`.
4. Scrivere il bit in `IPI_MASK_CLR` (se `IPI_ACK` non fosse stato dato, la linea si riasserirebbe).

Usi nel codice:
- **Linux, versione originale `L0`:** su M1 usava solo IPI legacy. Il driver conteneva `AIC_IPI_SEND`, `ACK`, `MASK_*`, un multiplexer software a 32 vIPI e i flag atomici `aic_vipi_flag`/`aic_vipi_enable`; il commento diceva "Fast IPIs (TODO)" e "sysreg-based IPIs are not supported yet" **[V: L0:irq-apple-aic.c:26-27,82-87,108,158,185-186,595-682]**. Quindi Linux ha avviato un SMP a 8 core sul t8103 con le IPI legacy in IRQ, nella sua prima versione (aprile 2021) **[V come codice; il fatto che funzionasse su hardware è I, ma il driver è stato accettato upstream con Reviewed-by e Acked-by]**.
- **Linux corrente:** con il sysreg fast IPI il percorso legacy resta "come fallback" (`use_fast_ipi` disattivato per i compatibili senza fast IPI) **[V: L:commit 2cf6821166 "The legacy IPI code is left as a fallback"; L:834-844, 1043-1044]**. Per t8103 è però selezionato `aic1_local_fipi_info`, cioè Fast IPI, quindi il codice legacy **non è più esercitato su t8103** nel kernel attuale **[V: L:309-313]**.
- **m1n1:** `smp_send_ipi` e il ciclo di attesa dei core secondari usano IPI legacy solo se `!cpu_features->fast_ipi` **[V: M:smp.c:115-131, 478-489]**; per M1 `features_m1.fast_ipi = true` **[V: M:chickens.c:76]**. Idem: il ramo legacy non è esercitato su t8103 in m1n1 attuale.
- Il ramo legacy serve per i SoC senza Fast IPI (prima dell'A11, per la descrizione del binding: "Starting from A11, system registers may also be used" **[V: L:apple,aic.yaml]**) e **AICv2 non ha più IPI legacy affatto** **[V: L:commit 2cf6821166, messaggio]** (riguarda M1 Pro/Max, non t8103).

### 4.2 Bastano per inviare IPI tra 8 core?
**Sì come meccanismo di base [V come funzionalità, I come prestazioni].**
- Il t8103 ha 8 CPU; `IPI_SEND` ha 31 bit di destinazione, quindi 8 CPU stanno comodamente **[V]**. Linux richiede inoltre `AIC_WHOAMI == smp_processor_id()`: l'ordine dei CPU logici in Linux deve coincidere con l'ordine AIC **[V: L:900]**. Per il t8103 l'indice DT è E0..E3 = 0..3 e P0..P3 = 4..7 (reg `0x0..0x3` e `0x10100..0x10103`) **[V: L:t8103.dtsi:63-173]**; l'indice AIC dei P-core deve essere verificato su hardware [I].
- Limiti:
  1. **Una sola IPI "other" per CPU** (più una "self", che Linux non usa). Non c'è identità del mittente né payload; più mittenti si fondono in un solo evento. Linux risolve con un multiplexer software: 32 vIPI per CPU con flag atomici condivisi **[V: L:34-35, 850; L0:595-682]**. La HAL Windows dovrebbe fare lo stesso (una coda o una maschera atomica per CPU) [I].
  2. **Latenza:** non quantificata in nessuna fonte che ho letto. Le Fast IPI sono "even faster" (parole del binding) perché evitano MMIO **[V: L:apple,aic.yaml, descrizione]**, ma non c'è un numero [I].
  3. Ogni IPI costa un giro `EVENT` (lettura MMIO con barriera) + ACK + MASK_CLR, cioè 3 accessi MMIO più l'invio **[V: L:aic_handle_ipi 812-836]**.
  4. Non c'è modalità "deferred/nowake/retract": quelle esistono solo nelle Fast IPI **[V: L:177-180; D:system-registers.md]**.
  5. **Risveglio da WFI:** m1n1 sui core secondari fa `deep_wfi()` e poi, in modalità legacy, `aic_ack()` + `IPI_ACK` + `IPI_MASK_CLR` **[V: M:smp.c:118-132]**, quindi la IPI legacy sveglia un core in deep WFI, almeno nel codice previsto per hardware senza fast IPI [I su t8103].
- **Alternativa con più dimensioni:** usare hwirq AIC software (`SW_SET`) con `TARGET_CPU` per CPU, uno per core, per ottenere IPI separate senza multiplexer [I]; richiede di scegliere numeri hwirq non collegati ad hardware reale (non verificato quali).

---

## 5. È sicuro mascherare per sempre i FIQ (`PSTATE.F`)?

**Conclusione: sì, a condizione di spegnere tutte le sorgenti alla fonte; il solo mascheramento in PSTATE non basta.** Prove:

1. **Nessun evento di errore o watchdog arriva come FIQ.**
   - Nel gestore FIQ di Linux e di m1n1 le uniche cause sono quelle della sezione 1: IPI veloci, timer, PMU e PMU uncore. Nessun errore di sistema, nessun watchdog **[V: L:557-613; M:exception.c:395-461]**.
   - Il watchdog usa un hwirq AIC normale (`AIC_IRQ 338`), non un FIQ **[V: L:t8103.dtsi:1228]**.
   - Gli errori hardware passano da **SError** (m1n1 abilita SError/IRQ/FIQ sul core di boot e gestisce `exc_serr` separatamente) **[V: M:exception.c:157-158, 463]**. PSTATE.F non influisce sugli SError (sono controllati da PSTATE.A) [I, ARM ARM].
2. **Linux stesso parte con tutte le FIQ spente alla fonte.** `aic_init_cpu` azzera Fast IPI pendenti, imposta `IMASK` su CNTP e CNTV, spegne i guest timer, il vGIC, la PMU (IMODE=OFF, IACT=0) e la uncore, poi `isb` **[V: L:859-892]**. Solo dopo abilita le sorgenti che gli servono.
3. **m1n1 fa lo stesso prima di mascherare/smascherare:** "Clear FIQ sources", poi `DAIF`: core di boot con tutto abilitato, core secondari con IRQ e FIQ disabilitati **[V: M:exception.c:133-160]**.
4. **Rischio di "FIQ storm":** Linux avverte che non gestire una sorgente attiva causa una tempesta di FIQ **[V: L:570-571]**. Con `PSTATE.F=1` non c'è tempesta, ma la linea resta asserita: un FIQ pendente (anche mascherato) tiene alta la linea e, secondo la regola ARM per WFI, farebbe uscire subito il core da WFI, impedendo un vero idle [I, ARM ARM, non verificato nei sorgenti Asahi]. Perciò bisogna anche **spegnere** le sorgenti, come fa `aic_init_cpu`, non solo mascherare.
5. **Stato lasciato da firmware o macOS:** il loader parte da iBoot/m1n1, quindi lo stato iniziale delle sorgenti non è garantito. Per questo m1n1 e Linux le azzerano a ogni boot di ogni CPU **[V]**. Il nostro firmware EDK2 e/o l'estensione HAL devono replicare **la sequenza di `aic_init_cpu` per ogni core** prima di passare a Windows [I, raccomandazione].
6. **Cosa Windows potrebbe riaccendere:** se Windows usa i timer architetturali (CNTP/CNTV) non può evitare di abilitarli. È il punto debole, vedi sezione 2. Se usa la PMU Apple (non standard, quindi improbabile senza driver) la riaccenderebbe [I].
7. **Un caso da non dimenticare:** `PSTATE.F` non basta se un livello superiore (EL2) instrada i FIQ via `HCR_EL2.FMO`: con FMO=1 il FIQ va a EL2 anche se EL1 lo ha mascherato [I, ARM ARM]. Il firmware che lascia Windows a EL1 deve avere FMO e IMO coerenti.

---

## 6. Conclusione

### 6.1 Tabella sorgente → si può evitare? → alternativa IRQ → confidenza

| Sorgente | Si può evitare? | Alternativa IRQ | Confidenza |
|---|---|---|---|
| Timer fisico/virtuale EL0 (`CNTP/CNTV`) | **Sì, se non si abilitano** (IMASK=1 / ENABLE=0) **[V: L:868-869]**. Ma Windows lo userà come timer architetturale | **Nessuna documentata.** Piste: watchdog WD0 via AIC IRQ 338 (sez. 3); `ACC_OVRD_CONVERT_SIQ_TO_IRQ` (sez. 2, ignoto) | Evitare: alta (V). Alternativa IRQ: **bassa** (I) |
| Timer guest EL02 (solo VHE/EL2) | Sì: `VM_TMR_FIQ_ENA_EL2=0` **[V: L:874-875]** e non girare a EL2 | Nessuna | Alta per l'evitare (V), rilevante solo se si usa EL2 |
| Fast IPI | **Sì**, non scrivere mai `IPI_RR_*`; azzerare `IPI_SR_EL1` all'avvio **[V: L:864-865]** | IPI legacy AIC (sez. 4) via `IPI_SEND`, ricevute come IRQ tipo 4 **[V: L:415-418]**; oppure hwirq software via SW_SET | Evitare: alta (V). Legacy funzionante su t8103: **media-alta** (V come codice storico `L0`, non collaudato oggi) |
| PMU core (E/P) | **Sì**: `PMCR0.IMODE=OFF`, IACT=0 **[V: L:882-883]** | `IMODE=2 (AIC)` esiste ma non è documentato quale hwirq; non serve a Windows | Evitare: alta (V). Alternativa: bassa (I) |
| PMC uncore | **Sì**: `UPMCR0.IMODE=OFF` **[V: L:886-889]** | Idem (`IMODE=2`) | Alta (V) |
| vGIC maintenance | Sì, non c'è EL2/KVM | n/a | Alta |
| Altre FIQ non note (es. `UPMSR` bit "CTI") | Sconosciuto | n/a | **Bassa** (I) |
| Errori e watchdog | Non sono FIQ: SError / AIC IRQ 338 **[V]** | n/a | Alta |

### 6.2 I tre rischi principali per la strategia "niente FIQ"

1. **Il tick di sistema.** Windows ARM64 usa di norma il timer architetturale, che qui è solo-FIQ. Non c'è nelle fonti nessun modo documentato di instradarlo come IRQ. L'unica alternativa visibile è il watchdog (IRQ 338), mai collaudato, con un solo comparatore utile, 32 bit che fanno il giro in circa 179 s, e il rischio di riavviare il Mac in recovery se si sbaglia il bit `RESET_EN` di WD0. Se la HAL timer non può essere sostituita, la strategia "mai FIQ" cade. Mitigazione: esperimento su hardware per `ACC_OVRD_CONVERT_SIQ_TO_IRQ`, per il WDT IRQ 338, o un piano B con un guscio minimo a EL2 (contrario al vincolo "niente hypervisor", decisione dell'utente). **Grado: critico. Prova disponibile: negativa/assente (V per l'assenza di documentazione; I per le piste).**
2. **Pulizia dello stato iniziale e sorgenti non note.** Nessun registro dice quale FIQ è attiva; basta una sorgente lasciata attiva da iBoot, m1n1 o macOS (IPI pendente, `PMCR0.IACT`, `UPMCR0`, `UPMSR` "CTI") per tenere il core in FIQ pendente e impedire l'idle o causare tempeste se un livello superiore le instrada. Mitigazione: replicare `aic_init_cpu` su ogni core nel firmware, verificare con un gestore FIQ diagnostico come `exc_fiq` di m1n1 su ogni core. **Grado: alto. Prova: V per la sequenza; I per le sorgenti sconosciute.**
3. **IPI legacy poco esercitate e con dettagli non confermati.** Il ramo legacy non è più usato su t8103 né in Linux né in m1n1; AICv2 (M1 Pro/Max e successivi) non ha più IPI legacy, quindi la strategia "mai FIQ" **non generalizza** oltre il t8103/AICv1 (obiettivo del progetto: tutti gli Apple Silicon). In più: una sola IPI per CPU senza identità del mittente (serve un multiplexer software), latenza non misurata, mappatura indice AIC/MPIDR da confermare, e le viste per-CPU `0x5000+` non documentate. Mitigazione: provare l'invio IPI legacy su t8103 tra 8 core con un test dedicato (cfr. `L0` come riferimento); per i SoC AICv2 prevedere hwirq software o altra soluzione. **Grado: medio-alto. Prova: V (codice `L0`), I (funzionamento attuale e prestazioni).**

### 6.3 Esperimenti suggeriti (richiedono il backup Time Machine confermato)
1. Con m1n1/EDK2: azzerare tutte le sorgenti FIQ come `aic_init_cpu`, mascherare `PSTATE.F`, eseguire WFI, controllare se il core resta in idle.
2. Test IPI legacy tra tutti gli 8 core con misura di latenza.
3. WD0: RESET_EN=0, BITE al massimo, BARK a pochi ms, IRQ_EN=1, osservare AIC IRQ 338.
4. Provare (con cautela) `ACC_OVRD.bit22` e vedere se un timer architetturale passa da FIQ a IRQ.
