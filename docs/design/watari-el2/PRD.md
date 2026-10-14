# PRD: Watari EL2 (strato EL2 minimo per Windows 11 ARM64 su Apple M1)

- Progetto: Watari Boot, sottosistema **Watari EL2** · Data: 2026-10-14 · Versione: 0.1 · Stato: draft
- Documenti collegati: [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md) · [DESIGN-STATUS](DESIGN-STATUS.md)
- ADR di riferimento: `docs/adr/bozza-piano-b-strato-el2.md` (decisione di Marco), `docs/adr/bozza-el2-base-m1n1-o-nuovo.md`, `docs/adr/bozza-el2-licenza.md`
- Marcatura: ogni affermazione tecnica è **[verificato: fonte]** oppure **[ipotesi]**. Le fonti `re/*` sono quelle del repository; le fonti web sono state lette in sola lettura il 2026-10-14.

## 1. Problema
Windows 11 ARM64 non parte sul M1 in modo nativo: richiede un GIC nella MADT (V2, F-14), non esiste un modo ufficiale per fornire un controller non-GIC (F-04, F-05), l'avvio dei core secondari col parking protocol va in deadlock (V3, F-15) e il timer è solo FIQ (F-03, F-13). Il piano B (bozza ADR) sposta il problema sotto Windows: uno strato a EL2 presenta a Windows un sistema "standard" (GICv3 + PSCI + timer come interrupt) e lascia tutto il resto nativo.

Questo documento definisce **cosa deve fare e come si misura** quello strato. Non tratta i driver Windows (ANS, DART, PEP, SPI-HID), che sono un altro sottosistema.

## 2. Obiettivi
- **O-1** Far partire Windows 11 ARM64 a EL1 sul MacBook Pro 13" M1 (t8103, j293) in dual boot, senza macOS sotto.
- **O-2** Presentare a Windows un **GICv3 virtuale** abbastanza fedele da non causare hang silenziosi (lezione F-14).
- **O-3** Fornire **PSCI via HVC** per l'accensione dei core.
- **O-4** Consegnare il **timer** ARM come interrupt virtuale (PPI) anche se l'hardware lo espone come FIQ.
- **O-5** Tradurre gli interrupt **AIC → SPI virtuali** e le **SGI → IPI AIC**.
- **O-6** Restare **minimi**: stage-2 identità, periferiche reali, 1 vCPU = 1 core fisico, nessuno scheduler.
- **O-7** Poter **sviluppare e testare quasi tutto fuori dal MacBook**: nucleo portabile, backend sostituibili, prototipo in QEMU TCG con `virtualization=on`.
- **O-8** Rispettare i quattro requisiti trasversali: mantenibilità, riproducibilità, modularità, estensibilità (sezione 7).

## 3. Non-obiettivi
- Nessun macOS o Linux sotto Windows. Nessun secondo guest. Nessuna migrazione, nessuno snapshot.
- Nessuna virtualizzazione annidata: Windows non deve usare Hyper-V/VBS/HVCI (non vedrà EL2) [ipotesi: da confermare in M2 che Windows non lo richieda].
- Nessun ITS, nessun LPI, nessun GICv4, nessun GICv2 legacy. MSI PCIe → SPI è fuori scopo della v1 (c'è solo la MSI Frame "vestigiale" richiesta da F-11).
- Nessun emulatore di periferiche (virtio, disco, rete, GPU): le periferiche sono reali e pilotate dai driver Windows.
- Nessun mondo secure / EL3.
- Nessun supporto a M1 Pro/Max/M2 (AICv2, errata diverse) nella v1: è un **punto di estensione**, non un obiettivo.
- Nessuna gestione energetica avanzata (sleep profondo, `CPU_SUSPEND` reale): `CPU_SUSPEND` si risolve come WFI.
- Nessun debugger interattivo (non si riscrive il proxy di m1n1).
- Il firmware EDK2 per M1 (piattaforma `AppleSiliconPkg`) e `acpigen` sono sottosistemi separati: qui se ne definiscono solo i **contratti** (INTERFACES, sezioni 2 e 9).

## 4. Utenti e attori
| Attore | Interesse |
|---|---|
| Marco (sviluppatore, utente finale iniziale) | avviare Windows sul proprio MacBook; sviluppo in QEMU sul Mac host |
| Ciani (secondo sviluppatore) | lavorare sui moduli indipendenti (vGIC, host test) |
| Ralph / agenti Sonnet | eseguire task piccoli con test automatici; richiedono test deterministici |
| Windows 11 ARM64 (attore di sistema) | consumatore dell'interfaccia GICv3/PSCI/timer |
| EDK2 (attore di sistema) | consumatore dell'handoff a EL1 |
| m1n1 (attore di sistema) | loader che consegna il controllo a EL2 |
| Utenti futuri di altri Apple Silicon | estendere con nuovi backend |

## 5. User stories
- **US-1** Come sviluppatore voglio avviare lo strato in QEMU TCG e vedere sulla seriale il banner, `CurrentEL=2` e la mappa stage-2, per sapere che le fondamenta funzionano.
- **US-2** Come sviluppatore voglio eseguire i test del vGIC sul Mac host (senza QEMU) in meno di 10 secondi, per fare TDD.
- **US-3** Come sviluppatore voglio che EDK2 arrivi al boot manager sopra lo strato in QEMU, per validare GICD/GICR/timer/PSCI con un guest vero.
- **US-4** Come sviluppatore voglio che Windows 11 arrivi alla schermata di blocco sopra lo strato in QEMU TCG con 4 vCPU, per validare la fedeltà.
- **US-5** Come utente voglio scegliere "Windows" all'avvio del MacBook e arrivare alla schermata di blocco (con tastiera/disco USB), per il traguardo finale della v1.
- **US-6** Come sviluppatore voglio un log dello strato leggibile anche dopo un blocco, per fare debug dove l'host non c'è.
- **US-7** Come sviluppatore futuro voglio aggiungere un SoC o un controller (AICv2) scrivendo solo un backend.

## 6. Requisiti funzionali
| ID | Requisito | Modulo (ARCHITECTURE) |
|---|---|---|
| FR-1 | Avviarsi a EL2 sia da m1n1 (payload in stile Linux Image + FDT) sia da QEMU (`-kernel`), inizializzare le CPU e consegnare EDK2 a EL1 con `x0` = FDT | `wel2-boot`, `wel2-fdt` |
| FR-2 | Stage-2 in identità per RAM e MMIO, con: regione dello strato non mappata, regioni GICD/GICR/MSI virtuali intercettate, attributi MMIO forzati a Device-nGnRnE | `wel2-s2` |
| FR-3 | Emulare il distributor GICv3 (GICD) con trap di stage-2, con i registri elencati in INTERFACES §4 | `wel2-vgic` |
| FR-4 | Emulare i redistributor GICv3 (GICR, 2 frame da 64 KB per vCPU) | `wel2-vgic` |
| FR-5 | Gestire l'interfaccia CPU virtuale: list register `ICH_LR<n>_EL2`, `ICH_HCR_EL2`, `ICH_VMCR_EL2`, maintenance interrupt, in due modalità selezionabili (`HW` e `TRAP_EMULATE`) | `wel2-vgic`, `wel2-cpuif` |
| FR-6 | Convertire gli interrupt fisici dell'AIC in SPI virtuali (INTID = 32 + n AIC), con mask/unmask coerenti con la semantica level | `wel2-pirq` |
| FR-7 | Intercettare i timer del guest (FIQ su M1, PPI su QEMU) e iniettarli come PPI virtuali 27 (virtual) e 30 (physical) a livello coerente | `wel2-vtimer` |
| FR-8 | Tradurre `ICC_SGI1R_EL1`/`ICC_ASGI1R_EL1`/`ICC_SGI0R_EL1` in SGI virtuali pendenti sulle vCPU destinatarie e in IPI fisiche | `wel2-vgic`, `wel2-ipi` |
| FR-9 | PSCI 1.x via HVC (`VERSION`, `FEATURES`, `CPU_ON`, `CPU_OFF`, `AFFINITY_INFO`, `MIGRATE_INFO_TYPE`, `SYSTEM_OFF`, `SYSTEM_RESET`, `CPU_SUSPEND` come WFI) e SMCCC base; ogni altra funzione risponde `NOT_SUPPORTED` | `wel2-psci` |
| FR-10 | SMP fino a 8 vCPU, 1 vCPU = 1 core fisico; avvio dei core secondari con meccanismo del backend (spin-table su M1, PSCI SMC di QEMU in TCG) | `wel2-psci`, `wel2-plat-*` |
| FR-11 | Gestire i trap dei registri di sistema con tabella dichiarativa; `ID_AA64PFR0_EL1.GIC` visto come 1; registri non in tabella: politica default-deny con log (RAZ/WI solo se in lista) | `wel2-trap` |
| FR-12 | Gestire SError routati a EL2 (errata SEIS del M1): classificare, registrare, e non propagare al guest salvo dichiarazione esplicita | `wel2-trap` |
| FR-13 | Esporre una MSI Frame (GICv2m) vestigiale, richiesta da Windows (F-11) | `wel2-vgic` (frame MSI) |
| FR-14 | Log a ring buffer in RAM riservata + seriale + framebuffer opzionale; panic con dump di registri, stato LR e ultimi N eventi | `wel2-log` |
| FR-15 | Contatori di uscite per causa (EC, sysreg, MMIO, IRQ, FIQ, SGI, MI) leggibili via HVC vendor e stampati periodicamente | `wel2-trap`, `wel2-log` |
| FR-16 | Backend sostituibili a build-time e a run-time dove possibile: `pirq` (aic-t8103, gic-qemu, fake), `ipi`, `timer-src`, `plat` (qemu-virt, t8103) | `wel2-plat-*` |

## 7. Requisiti non funzionali
| ID | Requisito | Misura |
|---|---|---|
| NFR-1 Prestazioni | Overhead del CPU-bound ≤ 3% rispetto allo stesso guest senza interrupt; ≤ 300 uscite/s/vCPU a Windows inattivo; latenza interrupt periferica (IRQ fisico → ISR del guest) ≤ 20 µs mediana su M1 | misurate in M3 con guest di controllo; soglie **[ipotesi]**, da ricalibrare dopo le misure di M2 (contatori FR-15) |
| NFR-2 Mantenibilità | ogni modulo ha responsabilità singola dichiarata; funzioni ≤ 80 righe; nessun `TODO` nel ramo principale; errori con codice e log; naming uniforme (INTERFACES §10) | revisione di Opus sul diff; lint (`-Wall -Wextra -Werror`, `clang-tidy` se già presente) |
| NFR-3 Riproducibilità | build identica bit-a-bit a parità di sorgenti: clang pinnato (versione scritta nel repo), `-ffile-prefix-map`, niente `__DATE__`; test con seed fisso; QEMU 11.1.2 e EDK2 `edk2-stable202608` pinnati; immagini identificate da SHA-256 | `make repro-check` costruisce due volte e confronta gli hash |
| NFR-4 Modularità | il vGIC non include header di piattaforma; dipendenze solo verso interfacce (`*_ops`); grafo aciclico verificato da script (`tools/check-deps.sh`) | script in CI locale |
| NFR-5 Estensibilità | aggiungere un SoC = nuova cartella `plat/<soc>` + voce in tabella; aggiungere un dispositivo emulato = registrare una regione MMIO; aggiungere un registro sysreg = una riga di tabella; nessuna modifica a dispatcher o vGIC | esercizio di prova in M3 (aggiunta di un handler fittizio) |
| NFR-6 Sicurezza hardware | nessuna scrittura al disco o al boot policy dallo strato; solo lettura di ciò che non serve; ogni accesso MMIO fuori mappa → fault registrato, non propagato | revisione + test "accesso non mappato" |
| NFR-7 Dimensione | ≤ 12.000 righe di C (esclusi test) e immagine ≤ 256 KB senza EDK2 **[ipotesi: KVM vgic ≈ 4-5 mila righe, m1n1 hv ≈ 3.700, `re/pianoB-fattibilita.md` §1-2]** | `cloc` |
| NFR-8 Licenze | nessun codice copiato da KVM/m1n1/NTASP; header di provenienza sui file che citano fonti (vedi `bozza-el2-licenza.md`) | scansione di similarità + revisione |
| NFR-9 Osservabilità | ogni hang deve lasciare una traccia: watchdog software per vCPU e dump al timeout | test di fault injection |

## 8. Criteri di successo per milestone (misurabili)
Stime di calendario: **[ipotesi]**, un sviluppatore con agenti (vedi `re/pianoB-fattibilita.md` §9: prototipo QEMU 8-12 settimane, hardware altre 14-24).

| Milestone | Cosa | Criterio di superamento (tutti) | Stima |
|---|---|---|---|
| **M0 (spike)** | Fondamenta in QEMU TCG | (a) `qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 -cpu max -kernel wel2.elf` stampa banner e `CurrentEL=2`; (b) stage-2 attivo; (c) un payload EL1 di prova (≤ 200 righe asm/C, nel repo) riceve 1 PPI 30 iniettato e fa `ICC_IAR1`/`ICC_EOIR1`; (d) test host del vGIC: ≥ 40 test, 100% verdi, < 10 s | 1-2 sett. |
| **M1** | EDK2 a EL1 sopra lo strato in QEMU TCG | (a) il nostro EDK2 (`WatariQemuPkg`) arriva al Boot Manager UEFI con `-smp 4`; (b) seriale: `ArmGicDxe` rileva GICv3, `[Bds]BdsWait` termina (regressione F-08); (c) EDK2 esegue `PSCI SYSTEM_RESET` e lo strato lo gestisce; (d) 0 trap non gestiti nel log; (e) 3 esecuzioni consecutive identiche negli eventi chiave | 3-5 sett. |
| **M2** | Windows 11 ARM64 sopra lo strato in QEMU TCG | (a) il Windows installato (N2/V1) arriva alla schermata di blocco con `-smp 4` (screenshot + log); (b) 4 vCPU accese via `CPU_ON` (4 voci nel log) e nessuna spenta per errore; (c) 0 SError non classificati, 0 trap di sysreg sconosciuti non-RAZ; (d) 30 minuti di inattività senza panic né `0x5C`; (e) tempo di boot ≤ 1,5× quello dello stesso Windows su TCG senza strato (baseline misurata con lo stesso script); (f) test di fedeltà vGIC (TEST_STRATEGY §4) 100% | 5-8 sett. |
| **M3a** | Strato su M1 con m1n1 e EDK2 | (a) autotest su hardware (SW_SET AIC → vSPI, IPI veloce, timer FIQ → vPPI 30 e 27, `CPU_ON` degli 8 core) 100% verde; (b) EDK2 arriva al UEFI con framebuffer; (c) overhead misurato con guest di controllo | 6-10 sett. |
| **M3b** | Windows sul MacBook | Windows 11 ARM64 da USB-C arriva alla schermata di blocco, con 8 CPU visibili; 0 panic in 30 minuti | 8-14 sett. |

Il passaggio da una milestone alla successiva richiede la revisione di Opus e la conferma di Marco per qualsiasi operazione sul disco/boot (M3 in poi).

## 9. Success metrics (oltre ai criteri)
- Fedeltà: percentuale di letture MMIO dell'oracolo (trace di QEMU GICv3) riprodotte correttamente = 100% sui registri statici.
- Stabilità: ore di Windows senza panic dello strato (obiettivo M3b: ≥ 2 h).
- Diagnosticabilità: ogni panic fornisce causa, PC, EC, ultimi 64 eventi (verificato con fault injection).
- Tracciabilità: ogni FR/NFR ha almeno un modulo e un test (tabella in ARCHITECTURE §10).

## 10. Scope e out-of-scope (riepilogo)
- **In scope:** un solo SoC (t8103), un solo guest, GICv3 senza ITS, PSCI, timer, SGI, SMP fino a 8, banco QEMU TCG.
- **Fuori scope:** vedi §3; in più ANS/DART/PEP/SPI-HID e la piattaforma EDK2 per M1.

## 11. Dipendenze con gli altri sottosistemi
| Da | Cosa serve | Stato |
|---|---|---|
| `tools/acpigen` | modello `qemu-virt` con FADT PSCI+HVC (già in uso nel controllo T1 di F-15); modello nuovo `t8103-el2` per M3 | parziale |
| `edk2-apple/WatariQemuPkg` | firmware che accetti `x0 = FDT` e parta a EL1 | esiste per QEMU |
| EDK2 per M1 | lettura FDT (non ADT) per la mappa di piattaforma | da fare (domanda non bloccante Q-1 in RISKS §8) |
| Asahi installer / m1n1 | caricare lo strato come payload | da verificare in M3a |

## 12. Rischi
Sintesi (dettaglio in [RISKS](RISKS.md)): errata SEIS del M1; fedeltà del vGIC; E2H bloccato a 1 sul M1; percorso FIQ del timer non testabile in QEMU; prestazioni del trap-emulate; debug senza host; licenze (m1n1 ≥ 2.0.0 è GPL).

## 13. Riferimenti agli ADR
- `docs/adr/bozza-piano-b-strato-el2.md` (accettato da Marco) · `docs/adr/bozza-el2-base-m1n1-o-nuovo.md` · `docs/adr/bozza-el2-licenza.md`.
- ADR-0001 (nativo senza hypervisor) è modificato dalla bozza del piano B; ADR-0007 (strada indipendente da NTASP) resta valido.
