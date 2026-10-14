# Fallimenti e lezioni imparate

Questo file raccoglie tutto quello che **non** ha funzionato, perché non ha funzionato e cosa si è imparato. Serve a noi e a chiunque provi a fare qualcosa di simile: un vicolo cieco documentato fa risparmiare settimane a chi viene dopo.

Formato di ogni voce:
- **Cosa abbiamo provato** e **cosa è successo**, con le prove (log, file, link);
- **perché** (verificato oppure ipotesi);
- **lezione** per chi legge.

---

## F-01 · Analizzare i binari di Windows (ntoskrnl/HAL) per la Fase 0 (2026-10-12)
- **Provato:** delegare a un agente il download di `ntoskrnl.exe` ARM64 con i simboli pubblici, per analizzarlo con Ghidra.
- **Successo:** il task è stato fermato da un filtro di sicurezza mentre veniva scritto.
- **Lezione:** la Fase 0 è stata ripensata come sperimentale ("scatola nera": documentazione pubblica + esperimenti in QEMU), vedi la bozza di ADR "Fase 0 sperimentale". Un progetto simile deve mettere in conto fin dall'inizio questo limite di metodo.

## F-02 · Ripartire dal lavoro di NTASP (2026-10-12)
- **Provato:** valutare il progetto esistente "NT for Apple Silicon" come base.
- **Successo:** non è nativo (usa l'hypervisor m1n1 con vGIC e PSCI emulati); l'estensione HAL AIC non esiste ("Nothing's implemented yet"); il driver AICv1 è uno stub; c'è un bug in `AIC_MASK_BIT` (segnalato: NT-for-ASi/apple_silicon_platforms_mu#23).
- **Lezione:** prima di costruire sul codice di altri, va controllata la qualità e non solo l'esistenza (`re/ntasp-audit-qualita.md`). Decisione: strada indipendente (ADR-0007).

## F-03 · "Basta non usare le FIQ" (ADR-0004), 2026-10-12/13
- **Provato:** progettare un M1 in cui Windows riceve tutto come IRQ.
- **Successo:** il **timer architetturale ARM del M1 è cablato solo in FIQ** (DTS `t8103.dtsi`, `AIC_FIQ`). Non esiste un modo documentato per averlo in IRQ, e macOS stesso lo usa in FIQ. Il bit `ACC_OVRD_CONVERT_SIQ_TO_IRQ` esiste in m1n1 ma nessuno l'ha mai usato (`re/f0-m1-fiq-irq.md`, `re/f0-timer-irq-hw.md`).
- **Lezione:** l'ipotesi "niente FIQ" era sbagliata in partenza per il timer. Le piste rimaste sono in `docs/adr/bozza-strategia-timer-fiq.md`.

## F-04 · Il precedente del Raspberry Pi 3 come prova (2026-10-13)
- **Provato:** usare Windows su Pi 3, che non ha GIC, come prova che un controller non-GIC si può fornire da fuori.
- **Successo:** il supporto al BCM2836 era **dentro l'HAL Microsoft** (`edk2-platforms` `Platform/RaspberryPi/AcpiTables/Madt.aslc:51-52`: "Windows 10 on Raspberry Pi 3 does not boot otherwise... value that Microsoft had in their IoT blobs").
- **Lezione:** "Windows gira su un chip senza GIC" non vuol dire "un terzo può aggiungere un controller". Vedi anche F-05.

## F-05 · Estensioni HAL per controller di interrupt (2026-10-13)
- **Provato:** cercare un meccanismo ufficiale con cui un'estensione HAL fornisca il controller di interrupt.
- **Successo:** secondo l'autore di `Wack0/NtHalExts`, le estensioni HAL per i controller di interrupt *"were added in 789x but removed again in 799x"*, cioè rimosse prima di Windows 8 RTM. Il suo `halextbcm2709` funziona cercando per pattern le funzioni interne non esportate, ed è collaudato solo su Windows 8 ARM32. Gli unici esempi pubblici moderni (i.MX) sono timer e DMA.
- **Lezione:** non esiste una via ufficiale; resta quella "con funzioni interne", fragile (`re/f0-nthalexts-pista.md`).

## F-06 · Togliere il GIC in QEMU per gli esperimenti (2026-10-12)
- **Provato:** usare QEMU `virt` senza GIC.
- **Successo:** `gic-version` accetta solo 2/3/4/x-5/host/max; `-acpitable` vale solo per i386.
- **Lezione:** in QEMU il "non-GIC" si simula solo nelle tabelle ACPI, quindi gli esperimenti danno un "no" affidabile ma solo un "sì" debole. Un vero banco senza GIC richiede un modello AIC scritto da noi (`re/f0-qemu-aic-modelli.md`).

## F-07 · Percorsi con spazi (2026-10-13)
- **Successo:** `edksetup.sh` di EDK2 e il comando `screendump` del monitor QEMU falliscono se il percorso contiene spazi (il disco si chiama "Arthur Fonzarelli").
- **Lezione:** si usa il link `/tmp/watari-data` (creato da `tools/env.sh`). Meglio dare ai dischi di lavoro nomi senza spazi.

## F-08 · Accelerazione HVF con il nostro EDK2 (2026-10-13, **RISOLTO** 2026-10-14)
- **Provato:** avviare Windows in QEMU con `-accel hvf -cpu host`, `-M virt,gic-version=3` e il nostro EDK2 `edk2-stable202608` (DEBUG, CLANGDWARF).
- **Successo:** si blocca sul logo TianoCore ("Start boot option"); la seriale si ferma a `[Bds]BdsWait(5)` e una CPU resta al 100%. Lo stesso firmware con TCG funziona e arriva al Setup di Windows.
- **Ipotesi sbagliate escluse** (utili a chi prova lo stesso): build RELEASE, `kernel-irqchip=off`, `gic-version=2`, `highmem=off`, `-smp 1` e il mascheramento del timer (IMASK); la patch provvisoria `edk2-timer-imask.patch` è stata scartata.
- **Causa (verificata):** con `-cpu host` sotto HVF, il campo `ID_AA64PFR0_EL1.GIC` vale 0 (letto nel firmware: pfr0=0x1101000010110011), anche se l'interfaccia GICv3 è utilizzabile. `ArmGicDxe` (`GicV3Supported`) decide GICv2/v3 solo da quel campo, quindi caricava il driver **GICv2** su un GICv3: nessun IRQ (PMR=0, IGRPEN1=0), il timer virtuale restava pendente e `BdsWait` aspettava per sempre. Il firmware EDK2 incluso in Homebrew invece funziona (è una build diversa).
- **Soluzione:** `tools/edk2-hvf-gicv3.patch`: GICv3 anche se la piattaforma dichiara una base dei redistributor (`PcdGicRedistributorsBase != 0`); viene applicata da `tools/build-edk2.sh`. **Verificato da Opus:** con HVF il Setup di Windows compare (`logs/screens/win-hvf-180s.png`).
- **Lezione:** su Apple Silicon con HVF i registri ID della CPU possono non dire la verità sulle funzioni del GIC. Il firmware non deve fidarsi solo di quelli. Possibile segnalazione a EDK2 (da decidere con Marco).

## F-09 · Regole sui download violate dagli agenti (2026-10-13)
- **Successo:** due subagenti hanno salvato file temporanei (una copia del sorgente Linux `aic.c`, il PDF della specifica parking) nelle cartelle interne di Claude, nonostante il divieto di download nei prompt. Il backup era confermato e niente è finito nella repository.
- **Lezione:** i divieti nei prompt degli agenti non bastano. Bisogna dire esplicitamente "niente file temporanei" e controllare dopo.

## F-10 · Schermo nero scambiato per blocco dell'installazione (2026-10-14, falso allarme)
- **Provato:** controllare con `screendump` l'installazione automatica di Windows (HVF, ramfb, senza display).
- **Successo:** dopo ~2 ore lo screenshot era completamente nero, il disco non scriveva più e la seriale era muta: sembrava un hang. In realtà Windows aveva finito e lo schermo si era spento per inattività.
- **Lezione:** prima di dichiarare un blocco, mandare un tasto (`sendkey shift` dal monitor QEMU) e rifare lo screenshot. Il silenzio della seriale non dice nulla sul kernel di Windows (il debug seriale non era ancora attivo/verificato).

## F-11 · Windows non avvia con MADT+GTDT di acpigen (modello qemu-virt) (2026-10-14) — RISOLTO
- **Provato:** firmware `QEMU_EFI-watari.fd` (driver `WatariAcpiOverride`, che a ReadyToBoot sostituisce MADT e GTDT con quelle di `acpigen -m qemu-virt`) e avvio del Windows 11 installato (N2), con HVF, disco `snapshot=on`. Windows caricava `ntoskrnl.exe` e si bloccava (CPU 100%, logo TianoCore). Con il firmware originale: desktop in meno di 2 minuti (`logs/screens/n4w-base.png`).
- **Causa (verificato):** nella nostra MADT mancava la **GIC MSI Frame** (sottotabella tipo 0x0D, QEMU v2m a 0x08020000, SPI base 0x50, count 0x40). Senza MSI Windows non può usare gli interrupt MSI di NVMe/xHCI e si blocca dopo il caricamento del kernel (spiegazione ipotesi; l'effetto e' verificato dall'ablazione sotto). Il confronto campo per campo e' in `re/f11-confronto-acpi.md` (tabelle originali lette dalla RAM del guest).
- **Esiti delle ipotesi** (script `/tmp/watari-data/run-f11/t.sh`, 4 minuti, `snapshot=on`; "blocco" = ancora sul logo TianoCore a 230 s):
  - H0 (ripetizione del problema, firmware com'era): blocco. Riprodotto.
  - H1 (parking disattivato: version 0, address 0): blocco da solo. Non e' la causa.
  - H2 (GICD prima dei GICC come QEMU; GICD/GICR gia' identiche in valore): blocco (H1+H2). Non e' la causa.
  - H3 (flag GTDT come l'originale: solo NS EL1 ALWAYS_ON, resto 0): blocco (H1+H2+H3). Non e' la causa.
  - H4 (errori di layout/lunghezza): nessuno nei campi dei GICC/GICD/GICR (stessi offset e valori). Differenze benigne: MADT revisione 6 invece di 4, GICC da 82 byte invece di 80, OEM ID.
  - H5 (nuova, scoperta dal confronto): aggiungere la GIC MSI Frame (e il PMU GSIV 23 nei GICC): **desktop/schermata di login in meno di 120 s**. Ablazione: la sola MSI Frame (PMU 0) basta (lock screen a 120 s). La MSI Frame con parking v1, GICD dopo i GICC e flag GTDT tutti ALWAYS_ON (cioe' il modello originale di F-11 piu' MSI) arriva al desktop: parking, ordine e flag GTDT sono quindi irrilevanti con PSCI su QEMU/HVF.
- **V1 riuscito:** `logs/screens/v1-ok.png` (desktop; il dialogo "Tasti permanenti" e' causato dai nostri `sendkey shift` per svegliare lo schermo, vedi F-10). Modello finale: parking 0, GICD per prima, PMU 23, GICR Structure, MSI Frame, GTDT come l'originale.
- **Lezione:** prima di ipotizzare, confrontare con le tabelle originali. Il dump si fa con `pmemsave` dal monitor QEMU e la ricerca di `RSD PTR ` nella RAM (non serve la shell UEFI).

## F-12 · Build del driver con layout stantio (2026-10-14)
- **Provato:** modificata `acpigen_cpu`/`acpigen_madt_opts` in `acpigen.h` e rifatto `tools/build-edk2-watari.sh`.
- **Fallito:** EDK2 non rileva le modifiche agli header di acpigen (percorso fuori da PACKAGES_PATH): `WatariAcpiOverride.c` restava compilato con il vecchio layout di `acpigen_model`, quindi leggeva `Model->gtdt` a un offset sbagliato: "acpigen_build_gtdt errore -2" oppure puntatore errato e QEMU/HVF in abort (`Assertion failed: (isv)` in `hvf_handle_exception`, a circa 7 s, durante il firmware).
- **Rimedio:** `tools/build-edk2-watari.sh` cancella la cartella di build del modulo prima di compilare. Se si vede l'abort `isv` subito dopo "MADT acpigen installata", controllare prima se il driver e' stantio.

## F-13 · V6: timer in Group 0 a ExitBootServices ripristinato in Group 1 (2026-10-14)
- **Provato:** `WatariTimerFiqDxe` mette PPI 27/30 in Group 0 a ExitBootServices (IGROUPR0 -> 0xB7FFFFFF, `ICC_IGRPEN0`=1, `GICD_CTLR.EnableGrp0`); Windows 11 con `kernel-irqchip=off` e trace del GIC.
- **Successo:** la scrittura avviene (seriale e trace `0x10080 = 0xb7ffffff`).
- **Fallito:** nel trace, subito dopo (`IGRPEN1=0`, `ICENABLER0=0xffff00ff`) arriva `IGROUPR0=0xffffffff`: PPI di nuovo in Group 1, prima che il timer sia abilitato. Zero `hvf_inject_fiq`: nessuna FIQ consegnata, quindi non si sa se Windows tollererebbe la FIQ. In piu' il controllo (firmware V1) con `kernel-irqchip=off` non arriva alla schermata di blocco ma a Windows RE (boot lento, "ripristino automatico"), quindi il banco di prova e' degradato.
- **Perche' (ipotesi):** un re-init del GIC dopo ExitBootServices (firmware o HAL di Windows) azzera i gruppi; non identificato chi.
- **Lezione:** il metodo A da solo non basta. Prima identificare lo scrittore di IGROUPR0 (es. trace con timestamp e PC, o hook nel monitor), e capire perche' `kernel-irqchip=off` degrada il boot di Windows; poi decidere se serve il metodo B (patch QEMU, da non compilare senza il via di Marco).

## F-14 · V2: MADT senza GICD, Windows non arriva alla sessione (2026-10-14)
- **Provato:** modello acpigen `qemu-virt-nogicd` (come qemu-virt: 4 GICC + GICR Structure + MSI Frame, **nessuna GICD Structure**; MADT 412 byte invece di 436), firmware `QEMU_EFI-watari-nogicd.fd` (`tools/build-edk2-watari.sh --nogicd`). Windows 11, HVF, kernel-irqchip di default, `-smp 4`, `snapshot=on`, 240 s. Script `/tmp/watari-data/run-g2/t.sh`.
- **Risultato (verificato):** il seriale mostra "MADT generata da acpigen: 412 byte, 4 CPU" e la sostituzione; Windows carica `ntoskrnl.exe` e poi lo schermo resta identico (stesso hash) da 20 s a 220 s: ancora sul testo UEFI/logo TianoCore (ramfb non viene piu' aggiornato), nessuna schermata di blocco (V1, con la GICD, la raggiunge a 120 s), nessun bugcheck visibile, nessun reset. Screenshot `docs/img/v2-nogicd-100s.png` e `v2-nogicd-220s.png`. E' lo stesso sintomo di F-11 prima della MSI Frame.
- **Interpretazione:** Windows **richiede la GICD nella MADT** (conferma il rischio R-02). Limite: hang, non un codice di bugcheck, quindi non si sa cosa faccia il kernel (probabile fallimento dell'init dell'HAL/GIC senza base del distributore; ipotesi). Una sola esecuzione di 240 s, senza controllo parallelo (il controllo V1 e' F-11).
- **Lezione:** su un SoC senza GICD (AIC) bisogna o fornire una GICD fittizia/compatibile o scrivere la HAL extension che sostituisce il GIC; la sola MADT senza GICD non basta.

## F-15 · V3: parking protocol senza PSCI, Windows lo usa ma va in bugcheck HAL_INITIALIZATION_FAILED (0x5C) (2026-10-14)
- **Provato:** firmware `QEMU_EFI-watari-park.fd` (WatariParkingDxe + MADT parking v1 + FADT con PSCI_COMPLIANT=0), `-smp 4`, Windows 11, HVF, `snapshot=on`.
- **Verificato:** seriale: 4 mailbox (0x46300000, 0x462F0000, 0x462E0000, 0x462D0000), `CPU_ON` cpu1..3 -> PSCI 0, FADT installata con PSCI=0, MADT 436 byte con parking v1. A circa 17 s dall'avvio Windows **scrive** nella mailbox della CPU1: `ProcessorId = 1` e `JumpAddress = 0x110af7010` (visto dallo stub, che lo copia a +0xFF8 prima di azzerarlo come ack). Le mailbox di CPU2 e CPU3 non vengono mai toccate. Circa 15 s dopo (a ~32 s) bugcheck **HAL_INITIALIZATION_FAILED (0x5C)** con riavvio automatico, in ciclo (poi "Preparazione del ripristino automatico"). Screenshot `docs/img/v3-bugcheck-hal5c.png`. Prove: `/tmp/watari-data/run-v3/` (mbs.log, mb-v3c-*.bin).
- **Esito decisivo:** Windows ARM64 **usa il parking protocol** (non lo ignora) anche senza PSCI, ma l'avvio del primo AP fallisce e l'HAL non si inizializza. Nessuna schermata di blocco, quindi nessuna misura delle CPU visibili.
- **Perche' (ipotesi, da verificare):** la CPU1 salta all'indirizzo di Windows ma non completa l'handshake: lo stub ha lasciato l'AP in uno stato che Windows non si aspetta (X0 = indirizzo della mailbox e non altro; cache/MMU; registri GIC/sistema; Processor ID letto da Windows in modo diverso), oppure Windows aspetta un SGI/IPI di risveglio che lo stub non gestisce. Nel trace non c'e' ancora il PC dell'AP (`info cpus` sotto HVF non mostra il PC).
- **Lezione:** il test V3 e' utile e il parking e' riconosciuto; il prossimo passo e' capire cosa Windows si aspetta dall'AP dopo il salto (trace del GIC per l'SGI, disassemblato dello stub di Windows a 0x110af7010 nel dump della RAM, stato dei registri richiesto dalla spec Microsoft del parking v1).

### F-15, tentativo A (ultimo tentativo a tempo limitato, 6 test QEMU, 2026-10-14, branch `marco/a-ultimo-tentativo`)
Strumenti: bit `WATARI_PMODE` (build `--parking`; 1 FADT PSCI+HVC, 2 MADT senza parking, 4 niente CPU_ON, 8 stub senza GIC, 16 niente wake dei redistributor, 32 GPR azzerati al salto), diagnostica dello stub nella mailbox (+0xF00..0xF30), gdbstub di QEMU (`-gdb`, funziona sotto HVF: restituisce GPR e PC, NON i registri di sistema aggiornati) e trace del GIC con `kernel-irqchip=off`. Script e prove in `/tmp/watari-data/run-a/` (`t.sh`, `gdbregs.py`, `trace-t6.log`, `mb-*.bin`, `shots/`).
- **H1 controllo (T1), ESCLUSA come causa:** FADT di acpigen con PSCI_COMPLIANT|USE_HVC (arm_boot_arch=3) + MADT con parking_version 0, nessun CPU_ON nostro: Windows arriva alla schermata di blocco (`docs/img/a-t1-controllo-fadt-psci-lock.png`, ~100 s). La nostra FADT e' buona; il 0x5C e' legato al parking. Limite: non si e' misurato quante CPU Windows vede.
- **H2 stato del core al salto (T2), ESCLUSA:** lo stub registra l'ingresso: CurrentEL=EL1 (HVF senza virtualizzazione, come vuole la spec), SCTLR_EL1=0x30900180 (MMU/cache spente), DAIF=0x3c0, MPIDR 0x80000001/2, ICC_SRE_EL1=7 gia' all'ingresso, PMR=0, IGRPEN1=0. Tutto conforme alla Tab. 1 della spec. Con X0 = mailbox Windows salta davvero: l'AP arriva in codice kernel (VA 0xfffff80...), cpsr 0x800003c5 (EL1h, DAIF mascherati). La mailbox contiene il blocco di avvio dell'AP preparato da Windows (+0x10.. TTBR/TCR/MAIR, +0x330 = VA della routine di avvio): il PC dell'AP e' sempre routine+0x200, in 4 avvii diversi (T2, T3, T5), quindi e' fermo in un'attesa breve (WFE/WFI o loop corto), non crashato in un'eccezione casuale.
- **H3 CPU interface/GIC dello stub (T3, T5), ESCLUSA:** stub senza toccare ICC, redistributor non svegliati da noi, GPR azzerati (PMODE=56): l'AP resta fermo allo stesso punto (+0x200). Quindi non e' il nostro init GIC (PMR=0xFF/IGRPEN1) a bloccarlo. Attenzione, artefatto di misura: fermare e riprendere la VM con gdb (T3, T5) fa sparire il bugcheck (schermo fermo sul testo UEFI fino a 220 s): il timeout del BSP non si puo' osservare con gdb.
- **H3b interrupt di risveglio / GIC in-kernel (T6), ESCLUSA l'idea "bug del GIC in-kernel di HVF":** con `kernel-irqchip=off` (GIC in userspace) il 0x5C si ripete (schermate 0x5C e boot loop). Trace del GIC, ripetuto in ogni avvio: (a) il BSP (cpu 0) fa la sua init ICC (IGRPEN1=0, PMR=0, IGRPEN1=1, PMR=0xF0); (b) la cpu 1, dopo il salto, fa IGRPEN1=0, PMR=0, `GICR1 ICENABLER0=0xffff00ff`, `GICR1 IGROUPR0=0xffffffff`, IGRPEN1=1, e poi **non scrive piu' nulla** (manca il PMR=0xF0 che il BSP scrive subito dopo IGRPEN1=1); (c) le uniche SGI generate sono SGI 0 dalla cpu 0 con targetlist 0x1 e affinity 0, cioe' verso se stessa (1 per avvio): **Windows non manda nessuna SGI/IPI alla cpu 1**, ne' come "doorbell" del parking ne' dopo.
- **H4 CPU2/3 mai toccate:** coerente con quanto sopra: Windows avvia un AP alla volta e aspetta che il primo completi l'handshake; l'AP 1 resta fermo e dopo ~15 s il BSP fa bugcheck. Non c'e' nulla che dica che CPU2/3 siano il problema.
- **Causa NON trovata.** Stato: il parking e' riconosciuto e l'AP entra nel kernel, ma si ferma in un'attesa subito dopo l'init del suo GIC, e il BSP non gli manda alcun interrupt. Sospetto principale [ipotesi, non provato]: l'AP aspetta un evento/flag dal BSP (WFE/SEV su memoria, o un interrupt che nel percorso PSCI arriva da un altro canale) e il BSP aspetta l'AP: deadlock. Altre ipotesi vive: la pagina mailbox mappata da Windows come Device mentre l'AP la legge cacheable (coerenza); il blocco dell'AP che aspetta qualcosa che con PSCI e' gia' nel context_id.
- **Prossimi passi (richiedono altri test, il budget di 6 e' finito):** (1) trace identico (`kernel-irqchip=off`, stessi eventi) con il firmware di controllo T1 (FADT PSCI, i core li avvia Windows): se in quel caso la cpu 1 scrive PMR=0xF0 e prosegue, il confronto dice cosa manca; (2) lettura dell'istruzione al PC dell'AP (routine+0x200) per sapere se e' WFE/WFI o un loop su memoria: serve guardare codice Microsoft, quindi decisione di Marco (regola "niente analisi di binari Microsoft"); (3) provare a mandare noi una SGI alla cpu 1 dallo stub del BSP/firmware dopo il salto (hack: ripetere SEV/SGI da un driver).
- **Lezione:** `info registers`/sysreg via gdb non si aggiornano sotto HVF (solo GPR e PC), e fermare la VM falsa i timeout del guest: per i test di timing usare solo trace del GIC e `pmemsave`.

## F-16 · WatariParkingDxe: ASSERT in InvalidateInstructionCache (2026-10-14)
- **Provato:** firmware `--parking` costruito dal task G4; avvio con DEBUG.
- **Fallito:** `ASSERT [WatariParkingDxe] ArmCacheMaintenanceLib.c(47)`: in `ArmCacheMaintenanceLib` la `InvalidateInstructionCache()` senza intervallo e' un `ASSERT(FALSE)`; il driver di G4 la chiamava dopo aver preparato le mailbox, quindi il firmware si fermava al caricamento del driver (nessun avvio).
- **Rimedio:** `InvalidateInstructionCacheRange(P, SIZE_4KB)`. Rifatta la build (FV sempre 48 byte liberi, entra).
- **Lezione:** G4 era stata verificata solo con la build; i driver vanno avviati almeno una volta. In DEBUG, le funzioni "tutta la cache" di ArmCacheMaintenanceLib sono ASSERT: usare sempre le varianti Range.
