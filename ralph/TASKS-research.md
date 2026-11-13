# Ralph research loop: tasks for Ciani's study material

Branch `ralph/ciani-research`. Output in `docs/research/`, in Italian. One task per iteration. Edit this list freely before each night: add a task at the bottom, or reset a `[x]` to `[ ]` to redo it.

## Stanotte
- **Scadenza:** 03:30  (Ralph stops at the first iteration that starts after this time, so the usage window is free again in the morning)
- **Richiesta per stanotte:** (write here in your own words what you want to understand, e.g. "le origini del progetto e come l'AI l'ha impostato all'inizio"; leave empty to just continue the list below)
- **Pianificata:** no  (Ralph sets this to "sì" after turning the request into tasks)

## Pack format (every "decision pack")
File `docs/research/NN-<slug>.md`, about 2-4 pages:
1. **In una frase** - the decision, in plain words.
2. **Il problema** - what was in the way, with an everyday analogy first.
3. **Le alternative** - what else could have been done (from the repository's ADRs, design docs, research notes; [verified] with the file).
4. **La scelta e perché** - what was chosen and the reasons the repository gives.
5. **Collegamento al Tanenbaum** - which chapter and which topic of *Modern Operating Systems* explains the concept, and what to read first.
6. **Cosa abbiamo verificato davvero** - measurements, tests, hardware results that support it (e.g. `re/m3a-day2-results.md`), versus what is only planned or assumed.
7. **Dubbi e punti deboli** - an honest devil's-advocate section: risks, things that could make the choice wrong, open questions. Mark each as [hypothesis].
8. **Glossario** - the new terms of this pack.
9. **5 domande di autoverifica** - with short answers at the end of the file, under "Risposte".
10. **File da leggere nel repo** - in order, with one line each on why.

## Tasks
- [ ] R0 `docs/research/00-indice.md`: the project in 15 minutes in Italian (what is on Marco's Mac at power-on, who talks to whom, why Windows does not boot alone, what the layer does), a timeline of the project from `docs/sessions/` and `docs/STATUS.md`, and the index of the packs below. Criterion: a student who never saw the repo can tell the story in 5 sentences.
- [ ] R1 Decision pack: the native route first (HAL extension for the AIC) and why it was abandoned (`re/f0-*`, `docs/adr/draft-phase0-*`).
- [ ] R2 Decision pack: Plan B, a small EL2 hypervisor under Windows (`docs/adr/draft-plan-b-el2-layer.md`, `re/planB-feasibility.md`).
- [ ] R3 Decision pack: emulating a GICv3 in front of Apple's AIC (`docs/design/watari-el2/`, `docs/design/watari-el2-devirq/`, `el2/vgic/`).
- [ ] R4 Decision pack: EDK2 (UEFI) instead of U-Boot (`docs/adr/0002-*`, `docs/design/watari-edk2-m1/`).
- [ ] R5 Decision pack: m1n1 as the loader on the real Mac, and the L0-L5 ladder (`docs/design/watari-el2-m1boot/`, `docs/procedures/m3a-day*`).
- [ ] R6 Decision pack: `trap` vs `trap-notc` vs `hw` CPU-interface modes, with the day-2 counters (`el2/boot/README.md`, `re/m3a-day2-results.md`).
- [ ] R7 `docs/research/novita.md`: "what changed since the last night": summarise every PR merged into `main` since the date of the previous entry in `ralph/PROGRESS-research.md` (the first time: the last 7 days), each with what the AI decided, why, and one question Ciani could ask Marco. Leave this task as the last one; reset it to `[ ]` before each new night.
