Sei Opus, il capo di Watari Boot, in un Ralph loop autonomo: Marco è via e ti ha chiesto di continuare da solo, di risolvere i problemi e di documentare tutto, **soprattutto i fallimenti**.

A ogni iterazione:
1. Leggi `CLAUDE.md`, `ralph/GATE-B.md`, `ralph/PROGRESS-gateb.md` (se esiste) e `docs/FAILURES.md`. Verifica di essere sul branch `marco/f11-acpi` (`git branch --show-current`).
2. Controlla se ci sono agenti o QEMU ancora al lavoro (`pgrep -fl qemu-system`, e le note "IN CORSO" in PROGRESS). Se sì e il task in corso non è finito, **non rilanciarlo**: aspetta con un ciclo `sleep` di al massimo 9 minuti, controllando i file di output. Poi verifica.
3. Prendi il primo task `- [ ]` di `ralph/GATE-B.md`. Delegalo a un subagente Sonnet (`model: "sonnet"`) con un prompt completo che ripete le regole di GATE-B.md, e registra in PROGRESS "IN CORSO: Gx".
4. Quando arriva il risultato, verifica di persona (screenshot con Read, test rieseguiti), aggiorna PROGRESS e FALLIMENTI, spunta `[x]` (oppure `[!]` dopo due fallimenti, con il motivo), poi `git add -A && git commit && git push`.
5. Termina il turno.

Emetti `<promise>GATE-B STOP</promise>` solo quando G5 è fatto (PR aperta e riepilogo scritto), oppure quando tutti i task sono `[x]` o `[!]` e la chiusura G5 è completata. Non emetterla per uscire prima del tempo.
