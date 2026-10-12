# Avvio per Ciani (e per il suo Claude)

## 1. Accesso
- Marco aggiunge Ciani come collaboratore della repo privata `ZonatedCord/WatariBoot`, poi Ciani accetta l'invito da email o da GitHub.

## 2. Setup una tantum
```sh
gh auth login                      # se non è già fatto
gh repo clone ZonatedCord/WatariBoot ~/Documents/02_PROGETTI/WatariBoot
cd ~/Documents/02_PROGETTI/WatariBoot
./tools/install-hooks.sh           # blocca i push diretti su main
git config user.name "<Nome Cognome>"
git config user.email "<email GitHub>"
```

## 3. Configurare Claude Code sul Mac di Ciani
Le regole del progetto stanno nel `CLAUDE.md` della repo e Claude le legge da solo. In più servono due impostazioni **sulla macchina di Ciani**:
- Opus come modello principale (`/model opus`).
- Sonnet come modello predefinito dei subagenti. In `~/.claude/settings.json`:
  ```json
  { "env": { "CLAUDE_CODE_SUBAGENT_MODEL": "sonnet" } }
  ```
  Poi va riavviato Claude Code. Al primo agente che parte, controllare nel pannello che sia davvero Sonnet.
- Il plugin **ralph-wiggum** deve essere installato e attivo (serve il comando `/ralph-loop`).

## 4. Lanciare Ralph (Fase 0, Gate A)
Seguire `ralph/README.md`. In breve:
```sh
git switch main && git pull --rebase
git switch -c ralph/f0-gate-a && git push -u origin ralph/f0-gate-a
```
Poi in Claude Code, dentro la cartella della repo:
```
/ralph-loop "Esegui le istruzioni di ralph/PROMPT-fase0.md" --completion-promise "RALPH STOP" --max-iterations 15
```
Il Gate A non scarica e non installa niente (solo lettura web e strumenti già presenti: `clang`, `make`, `python3`, `git`). Il Gate B **non** va lanciato senza l'accordo di entrambi.

## 5. Regole da ricordare
- Ralph gira solo su `ralph/f0-gate-a`, e solo sul Mac di Ciani: Marco non lavora su quel branch.
- Il diario delle sessioni di Ciani va in `docs/sessions/AAAA-MM-GG-ciani-NN.md`.
- Tutto il resto è in `docs/CONTRIBUTING.md`.
