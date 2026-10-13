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

## F-08 · Accelerazione HVF con il nostro EDK2 (2026-10-13, APERTO)
- **Provato:** avviare Windows in QEMU con `-accel hvf -cpu host`, `-M virt,gic-version=3` e il nostro EDK2 `edk2-stable202608` (DEBUG, CLANGDWARF).
- **Successo:** si blocca sul logo TianoCore ("Start boot option"); la seriale si ferma a `[Bds]BdsWait(5)` e una CPU resta al 100%. Lo stesso firmware con TCG funziona e arriva al Setup di Windows.
- **Perché:** da diagnosticare (ipotesi: il timer generico sotto HVF; un agente ha preparato `tools/edk2-timer-imask.patch`, **non ancora verificato**).
- **Lezione:** finché non è risolto, gli esperimenti girano con TCG, che è 10–50 volte più lento.

## F-09 · Regole sui download violate dagli agenti (2026-10-13)
- **Successo:** due subagenti hanno salvato file temporanei (una copia del sorgente Linux `aic.c`, il PDF della specifica parking) nelle cartelle interne di Claude, nonostante il divieto di download nei prompt. Il backup era confermato e niente è finito nella repository.
- **Lezione:** i divieti nei prompt degli agenti non bastano. Bisogna dire esplicitamente "niente file temporanei" e controllare dopo.
