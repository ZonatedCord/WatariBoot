# Ralph loop: come lanciarlo

Ralph è Claude in loop: a ogni iterazione riceve lo stesso prompt (`PROMPT-fase0.md`), svolge **un** task di `TASKS-fase0.md`, verifica, fa commit sul proprio branch e ricomincia. Si ferma quando scrive `<promise>RALPH STOP</promise>` oppure al numero massimo di iterazioni.

## Prima di lanciarlo (checklist)
1. `./tools/install-hooks.sh` (una volta per clone): blocca i push diretti su `main`.
2. Repo aggiornata e branch dedicato:
   ```sh
   git switch main && git pull --rebase
   git switch -c ralph/f0-gate-a   # oppure: git switch ralph/f0-gate-a, se esiste già
   git push -u origin ralph/f0-gate-a
   ```
3. Avvia Claude Code **nella cartella della repo**, con Opus come modello principale. `CLAUDE_CODE_SUBAGENT_MODEL=sonnet` è già impostato in `~/.claude/settings.json`. Al primo agente che parte, controlla nel pannello che sia davvero Sonnet.
4. Nessun altro (né il socio né un'altra sessione Claude) deve lavorare sul branch `ralph/f0-gate-a`.

## Comando
```
/ralph-loop "Esegui le istruzioni di ralph/PROMPT-fase0.md" --completion-promise "RALPH STOP" --max-iterations 15
```
- 7 task, uno per iterazione, più margine per i tentativi ripetuti: 15 iterazioni bastano. Ogni iterazione costa token: **non togliere `--max-iterations`.**
- Per fermarlo in qualsiasi momento: `/cancel-ralph`.

## Mentre gira
- Il progresso si legge in `ralph/PROGRESS-fase0.md`, anche da GitHub, perché Ralph fa push a ogni task.
- Il Gate B (download, QEMU, ISO di Windows) **non** lo fa: richiede backup confermato, almeno 100 GB liberi e l'ok umano.

## Quando ha finito
Ralph apre una PR `ralph/f0-gate-a → main`. Si apre una sessione con Opus, che fa la review della PR. Poi un umano approva e fa il merge (squash).
