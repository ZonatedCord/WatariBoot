# Stato del progetto

**Aggiornato:** 2026-10-12 (sessione 01)
**Fase corrente:** 0, lo spike sul HAL di Windows ARM64 e i controller di interrupt non-GIC

## Traguardi
- [x] Piano approvato (`docs/PLAN.md`)
- [x] Repository creato
- [x] Nome: **Watari Boot**, con sottotitolo fisso "Windows nativo su Apple Silicon" (ADR-0006)
- [x] Sessione di progettazione (`docs/design/watariboot/`, ADR da 0001 a 0005)
- [x] Analisi e audit di NTASP: strada indipendente (ADR-0007), bug segnalato con la issue NT-for-ASi/apple_silicon_platforms_mu#23
- [ ] Fase 0: verdetto sì/no sull'estensione HAL per l'AIC
- [ ] Fase 1: prototipo in QEMU
- [ ] Fase 2: EDK2 sul MacBook
- [ ] Fase 3: kernel di Windows nativo
- [ ] Fase 4: desktop
- [ ] Fase 5: potenza piena
- [ ] Fase 6: tutti gli Apple Silicon

## Blocchi e necessità
- ✅ **Backup Time Machine confermato** (2026-10-12, completato alle 19:06). Destinazione: volume cifrato "Time Machine" sul disco esterno, con quota di 1 TB; backup orari automatici. Escluso `~/Library/Containers/com.apple.Safari/Data/Library/WebKit/WebsiteData` (file con nome troppo lungo che bloccava il backup). **La regola resta:** prima di ogni download si chiede comunque conferma all'utente.
- ⚠️ **Spazio sul disco interno: 56 GB liberi** (era 5,9 GB). Per il Gate B ne servono circa 100: mancano ancora circa 45 GB.
- Fase 0: il task sui binari Windows è stato fermato da un filtro di sicurezza. Serve decidere come procedere.
- Restore DFU: ✅ risolto. Il socio dell'utente ha un Mac (servono Apple Configurator e un cavo USB-C). Prima va tentato il **revive**, che non cancella i dati; il **restore** cancella tutto.
- Debug host (dalla Fase 2): consigliato un portatile o mini PC Windows x86 usato (m1n1 + WinDbg). Alternativa: Raspberry Pi 5 da 8 GB (minimo Pi 4 Model B da 4 GB) con cavo USB-A→USB-C, ma per WinDbg serve comunque Windows.
- Serve un **debug host** (un Raspberry Pi 5 o un vecchio portatile) dalla Fase 2.

## Da decidere (non bloccanti)
- **Organizzazione GitHub:** facoltativa e rimandabile. Il nome è da scegliere (`watari` è occupato, `watariboot` è libero). Va creata dal browser, poi si trasferisce la repo (la storia si conserva e il vecchio indirizzo viene reindirizzato). Riservare il nome prima di qualsiasi annuncio pubblico.

## Da dove si riparte
Vedi `docs/sessions/2026-10-12-marco-01.md`. **Prossima azione:** lanciare Ralph sul Gate A della Fase 0 (`ralph/README.md`): niente download, si può fare subito.
