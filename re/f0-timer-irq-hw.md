# F0 / A9: il bit `CONVERT_SIQ_TO_IRQ` e altre vie per avere il tick in IRQ (con protocollo HW-1)

Task A9 (issue #17), con il protocollo del test hardware HW-1 (issue #20). Ricerca in sola lettura con `gh api` e WebSearch. Nessun download, nessun clone, nessun binario Microsoft.

Convenzioni: **[V: repo/file:righe@ref]** = verificato leggendo quel file; **[I]** = ipotesi o deduzione. Le righe sono quelle di `cat -n`.

| Sigla | Repo e riferimento |
|---|---|
| `M` | AsahiLinux/m1n1, `main`, HEAD `fbec52a81f2b12a5cf75f83a653d46df0bbc36ce` |
| `D` | AsahiLinux/docs, `main`, HEAD `0d1f8917fa88745d62a4c05d802c4a7298c8616b` |
| `L` | AsahiLinux/linux, branch `asahi` |
| `X` | apple-oss-distributions/xnu, `main`, HEAD `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea` |

---

## 1. Il registro e il bit in m1n1

### 1.1 Definizione
- Registro: `SYS_IMP_APL_ACC_OVRD = sys_reg(3, 5, 15, 6, 0)`, cioè `s3_5_c15_c6_0` **[V: M:src/cpu_regs.h:541]**.
- Nome lungo: nel proxyclient è `ACC_OVRD_EL1`, "Apple Core Cluster Override" **[V: M:proxyclient/m1n1/apple_regs.json:253]**. Accanto c'è `ACC_EBLK_OVRD_EL1` (s3_5_c15_c6_1), "E-Block Override" **[V: apple_regs.json, riga dopo]**.
- Il bit: `#define ACC_OVRD_CONVERT_SIQ_TO_IRQ BIT(22)` **[V: M:cpu_regs.h:563]**. Non ha una descrizione: nel file i bit di questo registro non hanno commenti.
- Bit vicini, tutti da `cpu_regs.h`:

| Bit | Nome in m1n1 | Note |
|---|---|---|
| 19 | `DVFM_SAFE_VOL` | **[V: cpu_regs.h:560]** |
| 20 | `FRC_ZOUT_AMX_PWR_DN_TMR` | **[V: :561]** |
| 21 | `RC_ACK_INTR_LOCK` | **[V: :562]** |
| **22** | **`CONVERT_SIQ_TO_IRQ`** | **[V: :563]** |
| [24:23] | `VOL_UPD_ACK` | **[V: :564-565]** |
| [26:25] | `POWER_DOWN_CPM` | usato in `cpu_sleep` **[V: M:src/utils.c:256]** |
| [28:27] | `CPM_WAKE_UP` | **[V: :568-569]** |
| 29, 30, 31 | `DISABLE_CLK_DTR`, `DISABLE_PSW_OFF_ABORT`, `RESET_CPM` | **[V: :570-572]** |

  Il contesto è un registro di **power management del cluster** (CPM, SRM, retention, deep sleep, DVFM, PMGR). L'interrupt-related più vicino è `RC_ACK_INTR_LOCK` (bit 21). Quindi `CONVERT_SIQ_TO_IRQ` sta probabilmente in una parte del cluster che parla con il power manager, non nel percorso del timer del core [I].
- Dove nasce il nome: il commit `b067f2697` (Hector Martin, 18 aprile 2023) dice solo "Add ACC_OVRD register. Not used yet, but might be in the future" **[V: gh api commits/b067f2697]**. I nomi (anche `PRE_RESET_CLK_CNTS`, `CNVT_INC_TO_FULL_UPD`, ...) non sono nei sorgenti XNU pubblici: l'header XNU conosce solo i bit 13-18, 25-29, 32 e 34 **[V: X:pexpert/pexpert/arm64/apple_arm64_regs.h:29-42]**. L'origine dei nomi non è documentata [I: probabile fonte interna o dump di stringhe, non verificabile].
- La documentazione Asahi di `ACC_OVRD` elenca solo i bit di deep sleep (13-18, 25-29, 32, 34) e **non cita il bit 22** **[V: D:docs/hw/cpu/system-registers.md:361-376]**.

### 1.2 Tutti gli usi in m1n1 (src/ e proxyclient/)
Ricerca `CONVERT_SIQ` nell'organizzazione: **una sola occorrenza, la definizione** **[V: gh search `CONVERT_SIQ_TO_IRQ+org:AsahiLinux`]**. Gli usi di `ACC_OVRD` in generale:
- `M:src/utils.c:254-257`: in `cpu_sleep(deep)` con `SLEEP_GLOBAL`, `reg_mask(ACC_OVRD, DIS_L2_FLUSH_ACC_SLEEP_MASK, PWR_DN_SRM(3) | DIS_L2_FLUSH_ACC_SLEEP(2) | TRAIN_DOWN_LINK(3) | POWER_DOWN_CPM(3) | DISABLE_PIO_ON_WFI_CPU | DEEP_SLEEP)` **[V]**. Il bit 22 non si tocca.
- `M:proxyclient/m1n1/hv/__init__.py:663`: `ACC_OVRD_EL1` (e `ACC_CFG_EL1`) sono nell'insieme `ro` dell'hypervisor, cioè le scritture del guest non passano **[V: __init__.py:655-664, interpretazione dell'insieme `ro` come "scritture ignorate" è [I]]**.
- `M:proxyclient/m1n1/apple_regs.json:253`: solo il nome, **nessun fieldset** per il bit 22 **[V]**.
- Nessun uso in `src/chickens*.c` **[V: grep]**.

### 1.3 Valore osservato sul M1
- I dump di Asahi riportano `SYS_APL_ACC_OVRD s3_5_c15_c6_0 = 0x180010102001c207`, uguale su Icestorm e Firestorm, sia in m1n1 (dopo i chicken bits) sia **sotto Darwin 20.3.0 (XNU 7195.81.3)** **[V: D:docs/hw/cpu/system-register-dumps.md:239 e :610]**.
- I byte bassi sono `0x2001c207`: il byte dei bit [23:16] vale `0x01`, quindi **il bit 22 è 0** in entrambi i dump **[V: calcolo sul valore sopra]**.
- Conseguenza: sul M1, macOS riceve il timer in FIQ con bit 22 = 0 (valore di fabbrica o impostato dal firmware) **[V come dato]**. Cosa succede con bit 22 = 1 è sconosciuto [I].
- Non si sa se il registro sia per core o per cluster. Il nome ("Core Cluster") e l'esistenza di `ACC_EBLK_OVRD` suggeriscono il livello cluster, con una copia per blocco E/P, ma il contenuto identico tra Icestorm e Firestorm non lo dimostra [I].

---

## 2. Dove cercare altrove: cosa risulta

### 2.1 XNU (codice Apple open source)
- **Nessuna occorrenza** di `CONVERT_SIQ`, `SIQ_CFG` o di `s3_4_c15_c10_4` in XNU **[V: gh search su `X`, tre ricerche vuote]**. XNU non tocca mai il bit 22 di `ACC_OVRD`: gli usi sono tutti nel percorso deep sleep **[V: X:osfmk/arm64/machine_routines_asm.s:1055-1081]**.
- `ARM64_REG_CYC_OVRD_irq_*/fiq_*` (modo "disable" 2 sui bit [23:22] e [21:20]) sono in XNU e li imposta a zero all'avvio (`and ... ~(irq_mask|fiq_mask)`) **[V: X:osfmk/arm64/start.s:789; apple_arm64_regs.h:45-48]** e li imposta a "disable" prima della WFI profonda **[V: machine_routines_asm.s:1036-1037]**. È l'unico controllo IRQ/FIQ che XNU usa e riguarda il risveglio, non il tipo di segnale [I].

**Come XNU gestisce il timer del kernel su Apple Silicon [V]:**
1. Il "decrementer" del kernel è il **timer virtuale EL0**: `ml_set_decrementer` scrive `CNTV_TVAL_EL0` **[V: X:osfmk/arm64/machine_routines.c:2393-2405]**. Per le SoC con `HAS_APPLE_GENERIC_TIMER` (header `H15.h` e `H16.h`) usa invece registri Apple S3_x (`S3_1_C15_C15_4` = TVAL, `S3_1_C15_C0_5` = CTL, ecc.) **[V: machine_routines.c:2375-2388; H15.h:49 "Supports 24 MHz Apple timer"]**. Un test XNU dice che con `AIDR_EL1` bit 32 (AGT) il contatore è a 1 GHz, altrimenti a 24 MHz **[V: X:tests/apple_generic_timer.c:19-25]**.
2. `fiq_context_init` abilita il timer virtuale (`CNTV_CTL_EL0 = ENABLE`) e **maschera il timer fisico** (`CNTP_CTL_EL0 = IMASKED`) per precauzione **[V: machine_routines.c:2627-2646]**.
3. L'interrupt del timer arriva come **FIQ**: i vettori puntano a `fleh_fiq` **[V: X:osfmk/arm64/locore.s:542-547, 805-809]**, che chiama `sleh_fiq`. Questa classifica: IPI veloce (`S3_5_C15_C1_1 & PENDING`), poi timer (`ml_get_timer_pending`: `CNTV_CTL_EL0.ISTATUS`), poi PMI; tutto il resto cade nel ramo "non sappiamo se è un timer" e viene trattato come `rtclock_intr` **[V: X:osfmk/arm64/sleh.c:2583-2672; machine_routines.c:2548-2551]**.
4. Gli IRQ veri (`sleh_irq`) vanno al driver del controller di interrupt della piattaforma, che non è nel sorgente pubblico (la parte AIC è in un kext chiuso) [I].
5. Le IPI sono Fast IPI via sysreg (`HAS_IPI`, `gFastIPI`) e arrivano anch'esse in FIQ **[V: sleh.c:2594-2642]**.
6. Il WFE usa lo "event stream" dell'arch timer (`CNTKCTL_EL1.EVNTEN`), che è un evento per WFE e non un interrupt **[V: machine_routines.c:2600-2614]**.

**Quindi: XNU non usa mai un tick in IRQ su Apple Silicon.** Usa il timer ARM virtuale come FIQ. Non c'è alcun timer MMIO in uso nel percorso del kernel, e `pe_arm_init_timer` registra solo un `tbd_ops` vuoto sotto arm64 **[V: X:pexpert/arm/pe_identify_machine.c:247-275]**.

### 2.2 Linux Asahi
- `irq-apple-aic.c`: nessun riferimento a `SIQ`, `ACC_OVRD` o `CYC_OVRD`; i soli registri IMP-DEF tra timer e FIQ sono `VM_TMR_FIQ_ENA_EL2` e Fast IPI **[V: L:drivers/irqchip/irq-apple-aic.c, grep]**.
- L'unico commento sul perché non si può mascherare: "Only the guest timers have real mask bits, unfortunately." **[V: L:irq-apple-aic.c:502]**. Non c'è alcun commento che spieghi perché il timer non possa andare in IRQ; è semplicemente documentato come cablato su FIQ **[V: D:docs/hw/soc/aic.md:71-73, "bypasses AIC. It is wired straight to FIQ"]**.
- Un messaggio di avvio nella documentazione mostra "CPU features: detected: FIQs" e "AIC: initialized with 896 IRQs, 2 FIQs, 2 IPIs, 32 vIPIs" **[V: D:docs/platform/dev-quickstart.md:653,669]**.

### 2.3 Documentazione Asahi e progetti pubblici
- `D:docs/hw/cpu/system-registers.md`: **un solo registro parla di IRQ/FIQ del core**, ed è `s3_4_c15_c10_4`: "labeled SIQ_CFG_EL1 in m1n1. If a core writes 0x3 to its copy of the register, AICv2 will not send IRQs to that core. FIQs unaffected, as those are part of the core complexes themselves. (0x0 and 0x2 are known values that will enable IRQs on a core, but 0x0 seems to cause weird EL1 problems)" **[V: system-registers.md:735-737]**. Cioè **i FIQ sono interni al complesso dei core, gli IRQ passano per un blocco esterno** [V come testo, il senso è dell'autore].
- Nome completo in m1n1: `SIQ_CFG_EL1`, "System Interrupt Configuration (EL1)", encoding (3,4,15,10,4) **[V: apple_regs.json:227]**. m1n1 vi scrive `2` sui core con `siq_cfg` ("Enable IRQs (at least necessary on t600x)", "XXX 0 causes pathological behavior in EL1, 2 works") **[V: M:src/chickens.c:228-232]**. Il flag `siq_cfg` è impostato nella struttura con `.siq_cfg = true` a riga 78, nel blocco `features_m1` (iniziato a riga 69) **[V: chickens.c:69-78; vedi 136-141 dove `features_m1` serve anche M1 Pro e Max]** (che sia attivo anche sul t8103 puro è [I]: da leggere sul valore).
- Valore letto di `SIQ_CFG_EL1` sul M1: `0x3` sia in m1n1 sia in Darwin **[V: D:system-register-dumps.md:208 e :579]**. Se il dump è davvero di un M1 base, il valore 0x3 non blocca gli IRQ lì (l'AICv1 non lo usa) [I]. Questa contraddizione apparente con la frase "0x3 = nessun IRQ" suggerisce che il registro riguardi solo l'AICv2 [I].
- Le registrazioni del tipo `VM_TMR_LR_EL2`/`VM_TMR_FIQ_ENA_EL2` e `HACR_EL2` bit 20 sono descritte: il timer guest, quando scatta, imposta lo stato "pending" in un registro tipo list register del vGIC, e `HACR_EL2[20]` lo maschera lasciando il FIQ "pending" in `VM_TMR_LR` **[V: D:system-registers.md:493-497, 662]**.
- Nessun'altra fonte pubblica trovata che usi o descriva `CONVERT_SIQ_TO_IRQ`. Una ricerca web con i termini `ACC_OVRD`, `SIQ`, `s3_5_c15_c6_0` non ha restituito nulla di pertinente (solo la documentazione AIC e le discussioni sulla patch Linux del 2021) **[V: WebSearch]**.

---

## 3. Che cosa sono gli "SIQ"

**Non lo sappiamo.** Quello che c'è:
1. In m1n1, "SIQ" compare in tre contesti, e non è detto che siano la stessa cosa:
   - `SIQ_CFG_EL1`, "System **Interrupt** Configuration" (nome lungo in `apple_regs.json`): controlla se un core riceve gli IRQ dal controller (AICv2) **[V: apple_regs.json:227; system-registers.md:737]**.
   - `ACC_OVRD_CONVERT_SIQ_TO_IRQ` (bit 22).
   - `HID1_CONSERVATIVE_SIQ` (bit 44 di HID1) ed `EHID20_BLZ_CONSERVATIVE_SIQ` (bit 6 di EHID20) **[V: cpu_regs.h:80, 391]**. Qui il nome sembra riferirsi alla pipeline del core, **non** agli interrupt: il commento in `hurricane_zephyr.c` che accompagna `reg_set(HID1, HID1_CONSERVATIVE_SIQ)` dice "disable crypto fusion across decode groups" e "Original Name: ARM64_REG_HID1_disAESFuseAcrossGrp" **[V: M:src/chickens/hurricane_zephyr.c:20-26]**, cioè il nome m1n1 `CONSERVATIVE_SIQ` e il nome XNU non coincidono. Quindi in questi due casi "SIQ" potrebbe essere una sigla interna diversa (es. coda di issue) [I].
2. Nel contesto interrupt, "SIQ" potrebbe essere un'interruzione "di sistema" generata dentro il complesso dei core (come il timer, le IPI veloci e la PMU) e che il sistema converte in IRQ verso l'AIC; **ma è una congettura costruita sul nome**. L'unica prova a favore è che il registro `SIQ_CFG_EL1` ha "System Interrupt" nel nome e controlla gli IRQ [I].
3. Nessuna fonte che ho letto definisce l'acronimo.

---

## 4. Altre vie IMP-DEF per un'interruzione periodica in IRQ, con grado di prova

| # | Via | Grado di prova | Note |
|---|---|---|---|
| 1 | `ACC_OVRD[22]` `CONVERT_SIQ_TO_IRQ` | **Solo nome**, nessun uso né descrizione (§1). Bit 0 in tutti i dump del M1 | Probabilità che converta proprio il timer: bassa [I] (§6) |
| 2 | **Timer guest EL02 con `VM_TMR_FIQ_ENA_EL2`** (bit 0 = V, bit 1 = P, `s3_5_c15_c1_3`) | **Registro e semantica verificati** **[V: L:irq-apple-aic.c:187-189; D:system-registers.md:499-504; M:cpu_regs.h:749-751]**. Linux lo usa per mascherare davvero il FIQ del timer guest. Il comportamento "FIQ spento, ma stato conservato in `VM_TMR_LR_EL2`" è descritto **[V: system-registers.md:493-497]** | Non produce un IRQ: **spegne** il FIQ e tiene lo stato in un registro tipo list register. Per farlo diventare un interrupt in Windows servirebbe un EL2 che inietta un IRQ o FIQ virtuale (`HCR_EL2.VI/VF`), come fa `hv_update_fiq` **[V: M:src/hv/hv_exc.c:152-180]**. È un hypervisor e quindi piano B. Serve solo se Windows gira a EL2 con timer EL02 [I] |
| 3 | Test già fatti dal team m1n1 su `HCR_EL2.TGE`, `HACR_EL2[20]`, `VM_TMR_FIQ_ENA` con lettura di `ISR_EL1` e `VM_TMR_LR` | Esiste lo script `proxyclient/experiments/timer_test.py` **[V: M:proxyclient/experiments/timer_test.py:1-109]** ma **non ne conosciamo i risultati**: non sono nel repo | Utile come modello per HW-1 (§5); i risultati sono forse nella loro documentazione privata [I] |
| 4 | **PMU in modalità AIC**: `PMCR0.IMODE = 2` (AIC) e `UPMCR0.IMODE = 2` | Il valore "2=AIC" è documentato **[V: D:system-registers.md:558,622; M:cpu_regs.h:605]**. **Non si sa quale hwirq AIC** corrisponda né se funzioni sul t8103 | Un contatore PMC (es. cicli) programmato per fare overflow dopo N cicli sarebbe un "timer" periodico in IRQ, ma **conta cicli e non tempo**: con DVFS la frequenza cambia, quindi serve ricalibrare. Candidato [I] |
| 5 | **Watchdog WD0, IRQ AIC 338** | Il commento del driver Linux descrive il BARK con interrupt, il DT dà `AIC_IRQ 338` **[V: L:drivers/watchdog/apple_wdt.c:22-35; arch/arm64/boot/dts/apple/t8103.dtsi:1228, già in `re/f0-m1-fiq-irq.md` §3]** | Mai collaudato in Linux. Rischio: `RESET_EN` su WD0 riavvia in recovery |
| 6 | **Software IRQ dell'AIC (`SW_SET`)** e **IPI legacy** | Verificati come meccanismo (`re/f0-m1-fiq-irq.md` §3.4 e §4) | Non è un timer: serve una sorgente periodica esterna (1, 4 o 5) che lo inneschi |
| 7 | **Copia del contatore nell'AIC** a `0x8020/0x8028` (`CNTPCT`) | Documentata, ma è solo un contatore **[V: D:docs/hw/soc/aic.md:27-28]** | Nessun registro di confronto noto: non genera interrupt |
| 8 | `CYC_OVRD` bit [21:20] FIQ mode e [23:22] IRQ mode | Documentati e usati da XNU e m1n1 per l'idle **[V: X:apple_arm64_regs.h:45-48; M:utils.c:269-270]** | Solo "disable" (2), controllano il risveglio dei core. **Non** convertono FIQ in IRQ [I] |
| 9 | `SIQ_CFG_EL1` (s3_4_c15_c10_4) | Documentato: controlla se gli IRQ dell'AICv2 raggiungono il core **[V: system-registers.md:737]** | Non riguarda i FIQ ("FIQs unaffected"). Non è una via di conversione |
| 10 | Event stream dell'arch timer (`CNTKCTL_EL1.EVNTEN`) | Usato da XNU **[V: machine_routines.c:2600-2614]** | Produce un evento per WFE, non un interrupt |

Conclusione sezione: **una conversione FIQ→IRQ del timer non è documentata da nessuna parte.** Le sole due vie con un barlume di prova sono il bit 22 (da testare) e la PMU in modalità AIC (da testare, cicli e non tempo).

---

## 5. Protocollo HW-1 (issue #20): test di `ACC_OVRD[22]`

### 5.0 Obiettivo
Rispondere a una domanda sola: **con `ACC_OVRD[22] = 1`, il timer architetturale di un core M1 cambia modo di arrivare, da FIQ a IRQ?** Il test è in sola lettura/scrittura di registri di CPU dal proxy, senza toccare il disco.

### 5.1 Prerequisiti
- **Backup Time Machine confermato dall'utente prima di cominciare** (regola assoluta). Il test non tocca dischi né il boot, ma lo stato di partenza ne richiede uno, e l'ambiente (debug host con m1n1 e Python) richiede installazioni sul Mac host se è un Mac. **Nessun download né installazione finché il backup non è confermato.**
- Il MacBook Pro M1 (j293) con m1n1 in **modalità proxy**. Due strade documentate:
  1. Asahi installato con l'opzione "UEFI environment only" e **backdoor proxy mode** (`csrutil disable` + `nvram boot-args=-v` dal 1TR sul volume m1n1, attesa di 5 secondi al boot) **[V: D:docs/sw/m1n1-user-guide.md, sezione "Backdoor proxy mode"; D:docs/sw/tethered-boot.md]**. Questa modifica tocca solo il volume Asahi, non macOS, ma richiede conferma dell'utente prima di toccare il boot (vincolo di progetto).
  2. Installazione "tethered-only proxy mode" dell'installer in modalità expert **[V: D:tethered-boot.md]**.
- Un debug host (Linux, o macOS "meno testato") con Python 3.9+, `pyserial`, `construct` e `m1n1` clonato **[V: D:tethered-boot.md]** collegato con un cavo USB-C alla porta Thunderbolt del j293. Il dispositivo del proxy è `/dev/ttyACM0` su Linux o `/dev/cu.usbmodemP_01` su macOS; variabile `M1N1DEVICE` **[V: D:m1n1-user-guide.md, sezione "Proxy mode"]**.
- Un secondo canale: UART seriale fisica non necessaria (l'hypervisor basta) **[V: tethered-boot.md]**, ma il proxy USB basta per questo test.
- Il Mac sotto test deve essere alimentato dalla rete e senza dati critici in sessione.

### 5.2 Sequenza di comandi (proxyclient di m1n1)
Avvio: `python3 -i proxyclient/tools/shell.py` (lo script `shell.py` esiste **[V: elenco di proxyclient/tools]**; che importi `m1n1.setup` è [I], però `proxyclient/experiments/*.py` usano tutti `from m1n1.setup import *`, che espone `p` (proxy), `u` (utils) e `mon` **[V: M:proxyclient/m1n1/setup.py:12-25]**).

API verificate:
- `u.mrs(reg)` / `u.msr(reg, val)` leggono/scrivono un registro di sistema, eseguiti sul core del proxy tramite `p.call`; errori sollevano `ProxyError("Exception occurred")` perché usa il guard d'eccezione **[V: M:proxyclient/m1n1/proxyutils.py:178-231]**. Il parametro `call=` sceglie il livello: `None`/`"el2"`, `"el1"`, `"el0"`, `"gl1"`, `"gl2"` **[V: proxyutils.py:108-115]**.
- `u.inst(0xd5033fdf)` esegue un'istruzione (qui `isb`) **[V: timer_test.py:19]**.
- `p.read32(addr)`, `p.write32(addr, v)`, `p.set32`, `p.clear32`, `p.mask32` **[V: proxy.py:840-921]**.
- `p.smp_start_secondaries()`, `p.smp_call_sync(cpu, addr, *args)` per i core secondari **[V: proxy.py:1020-1030]**; come far girare codice utile per `msr` su un altro core con questi è [I] (serve un blob di codice o `u.exec` con `p.smp_call`, non verificato).
- Nomi dei registri usati dagli script: `CNTV_CTL_EL0`, `CNTV_TVAL_EL0`, `CNTP_CTL_EL0`, `CNTP_TVAL_EL0`, `CNTFRQ_EL0`, `ISR_EL1`, `DAIF`, `HCR_EL2` **[V: timer_test.py:16-105]**; `ACC_OVRD_EL1`, `CYC_OVRD_EL1`, `VM_TMR_FIQ_ENA_EL2`, `VM_TMR_LR_EL2`, `SIQ_CFG_EL1` esistono nel `apple_regs.json` **[V: apple_regs.json:227,243-244,253]**; l'importazione come costanti Python `ACC_OVRD_EL1` da `m1n1.sysreg` è confermata da `hv/__init__.py:663` **[V]**, per `SIQ_CFG_EL1` e `VM_TMR_*` è **[I]** (stesso meccanismo di generazione, non visto). In caso di dubbio usare la tupla `(3, 5, 15, 6, 0)` come fa timer_test.py (`HV_VTMR_CTL = (3, 5, 15, 1, 3)`) **[V: timer_test.py:8,12]**.

**Passo A: baseline, sola lettura.**
```python
from m1n1.setup import *
acc0 = u.mrs(ACC_OVRD_EL1)
cyc0 = u.mrs(CYC_OVRD_EL1)
siq0 = u.mrs((3,4,15,10,4))         # SIQ_CFG_EL1, [I] solo l'encoding verificato
ena0 = u.mrs((3,5,15,1,3))          # VM_TMR_FIQ_ENA_EL2
daif0, isr0 = u.mrs(DAIF), u.mrs(ISR_EL1)
freq = u.mrs(CNTFRQ_EL0)
print(hex(acc0), hex(cyc0), hex(siq0), hex(ena0), hex(daif0), hex(isr0), freq)
```
Atteso (dai dump Asahi, M1 Firestorm): `acc0 = 0x180010102001c207`, bit 22 = 0 **[V: system-register-dumps.md:239]**, `siq0 = 0x3` **[V: :208]**, `CNTFRQ = 24000000` **[V: dev-quickstart.md:670; pe_identify_machine.c:53]**. Annotare tutti i valori: servono per il ripristino. Se `acc0` ha il bit 22 già a 1, **fermarsi** e riferire.

**Passo B: controllo (bit 22 = 0), timer virtuale.** Per non farsi interrompere dal proxy, mascherare tutto, come `test_hv_timers` **[V: timer_test.py:34]**.
```python
u.msr(DAIF, 0x3c0)            # D A I F mascherati
def provare(ctl, tval, ms=100):
    u.inst(0xd5033fdf)        # isb
    u.msr(ctl, 0)
    u.msr(tval, int(freq * ms / 1000))
    u.msr(ctl, 1)             # ENABLE=1, IMASK=0
    out = []
    for i in range(6):
        time.sleep(0.05)
        out.append((hex(u.mrs(ISR_EL1)), hex(u.mrs(ctl))))   # ISR bit7=I, bit6=F; CTL bit2=ISTATUS
    u.msr(ctl, 3)             # IMASK|ENABLE, poi spegnere
    u.msr(ctl, 0)
    return out
print(provare(CNTV_CTL_EL0, CNTV_TVAL_EL0))
print(provare(CNTP_CTL_EL0, CNTP_TVAL_EL0))
```
Atteso: dopo 100 ms `CTL.ISTATUS=1` e `ISR_EL1.F=1` (bit 6), `I=0`, perché il timer è FIQ (**[I]**, deduzione dal §2; `ISR_EL1` come fa timer_test.py **[V]**; il significato dei bit I/F di `ISR_EL1` è architettura ARMv8 standard [I, ARM ARM]). **Questo è il controllo negativo**: se già qui compare `I=1`, il modello FIQ per il timer EL0 con `HCR_EL2` attuale non è come previsto e bisogna capire prima il perché. Va ripetuto anche leggendo `p.read32(AIC + 0x4200 + ...)` (stato delle linee HW, `AIC_HW_STATE` a `0x23b100000 + 0x4200`) **[V: aic_test.py:14,25; D:aic.md "HW_IRQ_MON"]**, per vedere se una linea dell'AIC cambia durante il test (atteso: nessuna). Il significato esatto di ogni bit è sconosciuto [I]: confrontare solo prima/dopo.

**Passo C: scrittura del bit 22.**
```python
u.msr(ACC_OVRD_EL1, acc0 | (1 << 22)); u.inst(0xd5033fdf)
print(hex(u.mrs(ACC_OVRD_EL1)))        # il bit ha mantenuto il valore?
print(provare(CNTV_CTL_EL0, CNTV_TVAL_EL0))
print(provare(CNTP_CTL_EL0, CNTP_TVAL_EL0))
u.msr(ACC_OVRD_EL1, acc0); u.inst(0xd5033fdf)       # ripristino SEMPRE
assert u.mrs(ACC_OVRD_EL1) == acc0
```
Usare `try/finally` per il ripristino. Eventualità:
- La scrittura solleva `ProxyError`: il registro è bloccato o inesistente in questo contesto (`apple_sysregs_unlocked` è `true` per M1 in m1n1 **[V: chickens.c:72, 46-62]**, quindi è inatteso) [I].
- La rilettura dà il bit 22 a 0: il bit non è scrivibile (RAZ/WI o gestito da hardware). Esito "fallimento" per la pista.

**Passo D: variazioni (solo se C non basta).**
1. Ripetere C dopo `u.msr((3,5,15,1,3), 0)` e `(...,3)` per escludere l'effetto del mask IMP-DEF.
2. Ripetere C con `SIQ_CFG_EL1` a 0x2 invece di 0x3 (`m1n1 chickens.c` scrive 2 **[V: chickens.c:231]**) per vedere se la conversione richiede che il percorso IRQ del core sia abilitato. Ripristinare a `siq0`.
3. Provare gli altri core: avviare i secondari (`p.smp_start_secondaries()`), ripetere il test su un core E e uno P con `p.smp_call_sync` (**[I]** su come eseguire i `msr`: richiede un blob di codice caricato in memoria; fare solo se il passo C sul core boot dà un esito positivo o ambiguo). Il bit potrebbe valere per cluster [I].
4. Per vedere un IRQ **vero** e non solo `ISR_EL1`, il passo successivo è smascherare solo `DAIF.I` (`u.msr(DAIF, 0x340)`: I libero, F mascherato; valore da ricalcolare con cura) e osservare se `exc_irq` di m1n1 gira e quale evento AIC legge (AIC `EVENT` a `+0x2004`, tipo bit 23:16, numero 15:0 **[V: re/f0-m1-fiq-irq.md §4.1]**). Che cosa fa il gestore IRQ di m1n1 con un evento sconosciuto è **[I]**: potrebbe stampare e proseguire, o bloccarsi. Eseguire solo dopo un esito positivo su `ISR_EL1`.

### 5.3 Cosa osservare
- Per ogni prova: `ISR_EL1` (bit 7 = IRQ pending, bit 6 = FIQ pending), `CTL.ISTATUS` (bit 2), valori di `CNTVCT_EL0`.
- Stato AIC prima/dopo: `AIC_HW_STATE` (`+0x4200`), `AIC_SW_GEN` (`+0x4000`), maschere (`+0x4100`), e `EVENT` (`+0x2004`) solo nel passo D4 (la lettura è un acknowledge) **[V: aic_regs.h già citato in `re/f0-m1-fiq-irq.md`; aic_test.py:14-31]**. Si può usare `mon.add(AIC + 0x4000, 0x200)` e `mon.poll()` come fa aic_test.py **[V: aic_test.py:69-80]**.
- Registri IMP-DEF: `ACC_OVRD`, `VM_TMR_FIQ_ENA_EL2`, `VM_TMR_LR_EL2` (`s3_5_c15_c1_2`: a EL2 con il timer guest), `IPI_SR_EL1`, `PMCR0` per essere sicuri che non siano loro la fonte di FIQ.
- Eventuali eccezioni sul proxy (`p.get_exc_count()`) **[V: proxy.py:802]**.

### 5.4 Criteri
- **Successo pieno:** con il bit 22 = 1, il timer scaduto fa salire `ISR_EL1.I` (bit 7) e **non** `ISR_EL1.F`; nel passo D4 arriva un evento IRQ nell'AIC con un tipo e un numero che si possono annotare (hwirq del timer). Segue la domanda successiva: è per core? vale per EL0 phys e virt? vale per EL02?
- **Successo parziale:** `I` e `F` entrambi alti, oppure `I` alto solo in uno dei due timer: il bit fa qualcosa ma non basta; documentare.
- **Fallimento:** `ISR_EL1` identico al controllo (solo `F`), o il bit non si scrive, o scrittura in eccezione. Esito che va riportato come "il bit 22 non converte il timer EL0 sul M1" e chiude la pista (a) di `re/f0-m1-fiq-irq.md`. Passare allora a PMU/AIC (§4, via 4) o al WDT.
- **Anomalia:** blocco del Mac, tempesta di FIQ, perdita del proxy: interrompere il test, ripristinare con il riavvio (§5.5), annotare il passo.

### 5.5 Ripristino sicuro
- Prima di scrivere: salvare i valori letti nel passo A (`acc0`, `siq0`, `ena0`, `cyc0`) anche su file dell'host.
- Ogni prova è chiusa con il ripristino del bit 22 (`u.msr(ACC_OVRD_EL1, acc0)`) in un `finally`, e i timer spenti (`CNTV_CTL_EL0 = 0`, `CNTP_CTL_EL0 = 0`). `DAIF` rimesso a `daif0`.
- Il test non scrive nulla su disco, NVMe, NVRAM, iBoot o 1TR, a parte l'eventuale abilitazione preliminare del backdoor proxy mode (§5.1) che è un'operazione separata con conferma dell'utente.
- **I registri si azzerano al riavvio?** Non verificato in nessuna fonte. Deduzione: sono registri di sistema del core, azzerati o riportati ai valori di reset a ogni reset del core, e m1n1 non scrive mai il bit 22 né `ACC_OVRD` in avvio normale **[V: grep, §1.2]**; i dump di m1n1 e di Darwin coincidono, il che suggerisce che il valore all'avvio sia deciso dal reset/iBoot e non da macOS **[V come dato, il resto è I]**. Conservare la possibilità che un riavvio "caldo" (es. `proxyclient/tools/reboot.py` **[V: elenco tools]**) non resetti un registro di cluster in un dominio di alimentazione sempre acceso; per sicurezza fare uno spegnimento completo (tenere premuto il tasto di accensione) e riaccendere, poi rileggere `ACC_OVRD` dal passo A e confrontarlo con `acc0` [I].
- Se il Mac non risponde: spegnimento forzato col tasto di accensione (macOS e recovery non sono toccati dal test).

### 5.6 Rischi
- Basso per il disco; **medio** per la stabilità: il bit sta in un registro di power management del cluster (CPM, SRM, DVFM, PSW). Un cambio indesiderato di un bit vicino sarebbe grave, perciò la scrittura è sempre `acc0 | (1 << 22)` letta-modificata-scritta, mai un valore costruito a mano.
- Non sappiamo se il bit ha effetti collaterali sul risveglio dei core, sulle IPI o sul deep sleep [I]. Il test non usa `cpu_sleep` né WFI profonde.

---

## 6. Quanto è promettente HW-1

Giudizio: **poco probabile che funzioni sul timer, ma economico e a basso rischio, quindi da fare**.

A favore:
- Il nome del bit dice esplicitamente "convertire in IRQ" e il registro esiste su M1: sembra una funzione che Apple può aver previsto (per esempio per SoC in cui il percorso IRQ del core sia usato diversamente) **[V per il nome, I per il significato]**.
- Il nuovo timer Apple generico (`HAS_APPLE_GENERIC_TIMER`, registri `S3_x`, 1 GHz) mostra che Apple modifica il percorso del timer nelle nuove SoC **[V: X:H15.h:49; tests/apple_generic_timer.c]**; questo non dice nulla di un'opzione IRQ [I].

Contro:
- Zero usi, zero descrizioni, bit a 0 anche in macOS (che usa il timer in FIQ) **[V]**.
- Il registro è dominato da bit di power management; "SIQ" è senza definizione (§3). Può essere un segnale interno al cluster legato alla gestione dell'energia (per esempio un segnale di risveglio) e non il timer [I].
- La documentazione Asahi dice che i FIQ "sono parte dei complessi dei core" e quindi non passano per il blocco che gestisce gli IRQ **[V: system-registers.md:737]**: se "SIQ" è un'interruzione che va verso il blocco IRQ, il timer ne è escluso per costruzione [I].
- Stima di probabilità che il bit converta il timer EL0: nell'ordine del **10%**, giudizio mio, non misurato [I].

Il valore del test è anche negativo: chiudere la pista `a` con un fatto verificato vale quanto trovarla. Tutti i comandi sono reversibili entro la sessione. In parallelo, il test della PMU in modalità AIC (§4 via 4) e del WDT IRQ 338 (A8/A10) restano le alternative con più probabilità di dare un'interruzione periodica in IRQ, a costo di una HAL timer non standard.
