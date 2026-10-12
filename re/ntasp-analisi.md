# Analisi NTASP (NT for Apple Silicon), stato al 2026-10-12

Metodo: solo lettura via `gh api` e WebFetch, niente clone né download. Etichette: [verificato] = letto direttamente nella fonte; [ipotesi] = deduzione mia.
Base URL API: https://api.github.com/repos/NT-for-ASi/<repo>. Le pagine del sito sono state lette dai sorgenti in `nt-for-asi.github.io` (commit 76abd81d, 2026-06-07).

## 1. Repository dell'org (org creata 2023-03-27, "Bringing Windows unofficially to Apple's ARM-based devices")

| Repo | Descrizione | Linguaggio | Licenza dichiarata | Ultimo push/commit | Contributori |
|---|---|---|---|---|---|
| m1n1_windows (fork di AsahiLinux/m1n1) | "m1n1, but adapted to run windows (aka with a GIC emulator)" | Python/C | MIT | 2026-08-25 (6dcca476, amarioguy) | 30 (marcan 1093, hoshinolina 474, ..., amarioguy 100: storia ereditata dal fork) |
| apple_silicon_platforms_mu | Project Mu per Apple silicon (WIP), firmware UEFI | C | BSD-2-Clause (GitHub) | 2026-09-08 (861dcc49) | 3 (amarioguy 343, JamiNeedsSleep 22, graphine27 21) |
| apple_silicon_nt_drivers | "Windows drivers and HAL Extensions" | nessuno | nessuna (README: MIT/GPL2) | 2025-08-28 (5fc3fe63) | 1 (amarioguy, 3 commit) |
| arm-trusted-firmware-apple | TF-A portato su Apple silicon | C | "Other"/NOASSERTION (BSD-3 di TF-A) | 2023-06-19 (d557aaec: è un merge di upstream Arm) | 30 (tutti upstream Arm) |
| nt-for-asi.github.io | sito (Zensical) | HTML | nessuna | 2026-06-07 (76abd81d) | 8 (Simizfo 53, gus33000 43, Orangera1n 12, ...) |
| .github | profilo org | - | - | 2026-05-09 | 1 |

[verificato] `gh api orgs/NT-for-ASi/repos`, `.../commits`, `.../contributors`.

Stato: il lavoro attivo e` di fatto una persona (Arminder Singh, `amarioguy`) piu' pochi collaboratori sul sito e su Mu. Ci sono commit recenti (agosto/settembre 2026) su m1n1_windows e Mu, quindi non e` fermo. Pero' `apple_silicon_nt_drivers` contiene SOLO un README (un file da 1519 byte, 3 commit del 2025-08-28) e dice "Nothing's implemented yet" [verificato: `.../git/trees/HEAD?recursive=1`, `.../readme`]. Quindi nessun codice dell'estensione HAL AIC e` pubblico. Il progetto stesso si definisce "hobby project maintained by a few people" [verificato: docs/project-info/when-will-ntasp-be-done.md].

Predecessore: `amarioguy/AppleWOAProject` e` archiviato ("Deprecated NTASP project website"), contiene solo README/index/_config, ultimo push 2026-05-06; nessun codice [verificato]. Lo stesso amarioguy ha un fork di Linux del 2022, irrilevante.

## 2. Architettura di boot effettiva

- Hypervisor: SI, nel percorso funzionante. `m1n1_windows` e` descritto come m1n1 "with a GIC emulator" e contiene `src/hv_vgic.c`, `hv_psci.c`, `hv_aic.c`, `hv_vm.c`, `hv_smmu.c`, `hv_vtpm.c`, `hv_vuart.c`, `hv_virtio.c` [verificato: tree di m1n1_windows]. L'intestazione di `hv_vgic.c` (commit 300b613a, 2026-07-20) dice che l'M1 implementa in hardware l'interfaccia CPU GIC e m1n1 deve emulare solo distributor, redistributor e eventualmente ITS (8 list register, 32 livelli di priorita`, 16 bit di ID) [verificato].
- Il README di Mu dice: "can be built with support for the platform's AIC if you intend to run this without a vGIC (the default), or ... assuming a vGIC" [verificato]. Il Readme del driver AIC (`Silicon/Apple/AppleSiliconPkg/Drivers/AppleAicDxe/Readme.md`) consiglia: "highly advised you run this EFI under a thin hypervisor providing a vGIC while the AIC support is experimental" [verificato]. Il DSC di T810XFamilyPkg, con AIC_BUILD==FALSE, mette il GIC a 0xf00000000 / 0xf10000000 "virtualized by trusted firmware", e dice che T8103 non ha un GIC [verificato: `Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc`].
- La MADT statica per M1 (`T810XFamilyPkg/AcpiTables/MADT_Static.aslc`, commit 19df1750, 2025-10-27) descrive GICC per core, distributor, redistributor e ITS: cioe` Windows vede un GICv3 virtuale [verificato]. Windows quindi gira come guest EL1 sotto l'hypervisor di m1n1 [ipotesi forte: coerente con tutti gli elementi sopra, ma non c'e` un documento che lo affermi in una riga].
- arm-trusted-firmware a EL2/EL3: esiste il fork `arm-trusted-firmware-apple`, ma e` fermo al 2023 e i suoi commit sono solo il merge di upstream Arm [verificato]. `hv_psci.c` dichiara: PSCI per guest sotto hypervisor, "a straight port of the implementation in ARM Trusted Firmware-A"; per il bare metal "we use a higher level firmware running in GL2 to provide PSCI, so this file does not account for that case" [verificato: src/hv_psci.c, commit 1e5adc1e, 2025-11-16]. Il caso bare metal con PSCI da GL2 e` quindi un'intenzione, non codice presente [ipotesi].
- Core secondari: nel percorso hypervisor, tramite PSCI emulato (CPU_ON) in `hv_psci.c`, con alberi di power domain per T8103/T8112/T600x, piu' `src/smp.c` e un branch `staging/smp` [verificato nei nomi file e nel sorgente; l'uso effettivo di CPU_ON da parte di Windows non e` verificato]. Non ho trovato nessun parking protocol: ricerche per "MpPark" e "ParkingProtocol" nell'org senza risultati, e nessun file con "park" nell'albero di Mu [verificato per assenza]. Il sito dice SMP "still a tad bit fragile" [verificato].
- Si noti la differenza dal nostro PLAN.md (parking protocol, niente hypervisor, FIQ mascherate): NTASP prende la strada opposta (vGIC + PSCI).

## 3. AIC e HAL

- Lato Windows: l'estensione HAL per l'AIC NON e` pubblica. Il repo driver e` vuoto e il sito dice "AIC: UEFI driver works, Windows HAL Extension needs more work" (m1.md) [verificato]. Non posso quindi citare file o funzioni dei driver NT: non esistono nel repo. Meccanismo di registrazione nell'HAL: non documentato [ipotesi: HAL extension via CSRT, perche' CSRT.aslc dice che l'ACPI serve a segnalare all'OS "the interrupt controller and IOMMU" non standard].
- Cosa esiste (lato UEFI): `Silicon/Apple/AppleSiliconPkg/Drivers/AppleAicDxe/` con `AicV1/AppleAicV1Dxe.c`, `AicV2/AppleAicV2Dxe.c`, `AppleAicCommonDxe.c`, `AppleAicLib.c`, `AppleAicV1.S`, `AppleAicV2.S`. Stato dichiarato: "AICv2 driver finished, AICv1 in progress" (quindi per l'M1, che e` AICv1, e` ancora in corso) [verificato]. Il sito invece dice "UEFI driver works" per M1: leggera incoerenza tra le fonti.
- FIQ/IPI: il Readme dice che i timer sono cablati come FIQ e che su AICv1 (M1) esistono sia IPI FIQ "fast" sia IPI legacy in IRQ; "if an AICv1 is detected, it will defer to using legacy IPIs" [verificato]. Vale per il driver UEFI. Per Windows: come gestiscano le FIQ non e` documentato [ipotesi: sotto vGIC l'hypervisor traduce].
- Il timer: in Mu ci sono `AppleArmGenericTimerPhyCounterLib` e `...VirtCounterLib` [verificato].
- Un punto in comune con il nostro piano: IPI legacy via IRQ su M1. Differenza: noi vogliamo AIC nativo nell'HAL, loro usano GIC virtuale.

## 4. Fin dove arriva il boot di Windows

- Mu README: dal USB-C esterno il UEFI parte ("not without hacks"), il Windows loader inizializza e crea il RAMDisk, ma "hangs on the handoff between bootloader and kernel" [verificato: README di apple_silicon_platforms_mu; non datato, ma il repo e` stato aggiornato il 2026-09-08].
- Sito: "we have Windows barely booting with some fundamentals working", lavorano su ANS e AIC "as of 1/1/2026", il sistema "stay alive for a portion of time (albeit very slowly ...) only boots tested were Windows to Go boots"; serve un secondo computer [verificato: when-will-ntasp-be-done.md]. Home: "currently not installable" [verificato].
- Le due affermazioni sono in tensione (hang al passaggio loader-kernel vs. kernel che resta vivo): il README Mu potrebbe essere indietro [ipotesi]. Non ho trovato log, video o screenshot nei repo o sul sito (nel sito c'e` solo il logo) [verificato per assenza nelle pagine lette; Discord non consultabile].
- Il "Windows to Go" implica un'immagine su USB, non un installer [ipotesi].

## 5. Feature support M1 (sorgente: docs/feature-support/m1.md, commit 76abd81d; https://nt-for-asi.github.io/feature-support/m1/) [verificato]

Blocchi SoC T8103 (valgono per tutti i dispositivi M1):

| Feature | Stato |
|---|---|
| CPU core (Firestorm/Icestorm) | Partially working (SMP "a tad bit fragile") |
| DeviceTree | Mostly working |
| AIC | UEFI driver works, Windows HAL Extension needs more work |
| NVMe | In progress |
| Thunderbolt 3 | Not working (pending Type-C bringup) |
| DP Alt Mode | Not working (pending Type-C bringup) |
| AGX GPU | Not working |
| Display Controller | Partially working (framebuffer di iBoot) |
| SEP | Not needed for now, optional |
| SMC | Not working |
| PCIe | Not working (pending DART bringup) |
| SPI | Not working |
| Wi-Fi + Bluetooth | Not working (dipende da PCIe) |

Per T600x la tabella e` identica, salvo CPU "multi-die untested".

MacBook Pro (M1, 2020) J293AP: Installer Not working; USB-C Partially working (solo input e storage parziale); Battery Untested; Built-in display Working; Keyboard e trackpad Not working (pending SPI); Touch Bar Not working.
Mac mini (M1, 2020) J274AP: Installer Not working; USB-A Partially working (solo input e storage parziale); Ethernet Not working (no drivers); HDMI Out solo framebuffer iBoot.
(Il MacBook Air J313AP ha le stesse righe del Pro senza la Touch Bar.)
Nota: "Built-in display: Working" e` coerente con il framebuffer di iBoot; non e` accelerazione.

## 6. Licenze

- m1n1_windows: MIT (file LICENSE, e `3rdparty_licenses`). Sorgenti propri con SPDX `MIT OR BSD-3-Clause` (hv_psci.c) e `BSD-2-Clause-Patent OR MIT` (hv_vgic.c) [verificato]. Obblighi: mantenere copyright e testo licenza; gli header citano il codice KVM vGIC (derivato di Linux, GPL-2.0: da verificare file per file) [ipotesi].
- apple_silicon_platforms_mu: BSD-2-Clause per GitHub; README: "dual licensed under either the BSD 2 clause license or the MIT license", con componenti "(MIT OR BSD-2-clause) AND GPL" per il codice derivato da Asahi, quindi GPL su quei file. I submodule Mu seguono la loro licenza (BSD-2-Clause-Patent) [verificato].
- apple_silicon_nt_drivers: nessun file di licenza. README: MIT di default, GPL2 per driver derivati da Linux [verificato]. Senza file LICENSE, il default legale e` la licenza dichiarata solo nel README: chiedere conferma [ipotesi].
- arm-trusted-firmware-apple: BSD-3-Clause di TF-A (file `license.rst`) [verificato].
- Forkare: possibile per le parti MIT/BSD (obbligo: conservare avvisi). Per le parti GPL, obbligo di rilasciare le modifiche sotto GPL. Il nostro CLAUDE.md gia' prevede header di provenienza. Contribuire: il README Mu dice "Contributions are always welcome! ... pull request" [verificato]. Windows: nessun codice Microsoft nei repo letti [ipotesi: non scansionati a fondo].

## 7. Come contribuire

- Canali: Discord https://discord.gg/djXD4hXyYB e GitHub org https://github.com/NT-for-ASi (link dalla home del sito) [verificato]. Issue/PR: Mu e m1n1_windows hanno 3 e 0 issue aperte [verificato].
- Maintainer principale: Arminder Singh (`amarioguy`), che firma quasi tutti i commit recenti [verificato]. Il sito e` curato da Simizfo, gus33000 (Gustave Monce, WOA Project), Orangera1n e altri.
- NTASP si dichiara NON affiliato ad Asahi Linux [verificato: faq.md]. Nell'issue AsahiLinux/m1n1#63 (aperta da lennylxx il 2021-05-21, chiusa) marcan rispose che Windows non girera` mai su bare metal senza la cooperazione di Microsoft, perche' richiede modifiche al kernel, ma si potrebbe in una "thin VM" che emula un sistema ARM SystemReady [verificato]. E` esattamente l'architettura scelta da NTASP.

## Valutazione per Watari Boot

- Nessun elemento dimostra che NTASP boota Windows senza hypervisor. [verificato per quanto pubblico]
- Parti riusabili: ACPI/CSRT/MADT per T8103 e le tabelle j293 (`Platform/MacBookProLate2020Pkg`), il driver AIC UEFI, DeviceTree parsing, `hv_psci.c` come riferimento per l'albero dei power domain.
- Parti NON disponibili: HAL extension AIC (nessun codice), kernel handoff, parking protocol.
- Limiti miei: non ho letto Discord, non ho aperto in profondita` ogni file di Mu; le righe di sorgente citate sono quelle viste via API.
