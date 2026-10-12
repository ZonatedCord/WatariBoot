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

## 4. Come lavora Ciani
- Ciani **non** usa Ralph: lavora a sessioni normali con il suo Claude (Opus capo + subagenti Sonnet).
- Prende i task dalle **GitHub Issues** assegnandoseli, e lavora su branch `ciani/<fase>-<tema>`. Ogni branch porta a una PR.
- Non tocca i file di `ralph/` né il branch `ralph/...`.

## 5. Regole da ricordare
- Ralph, se usato, gira solo sul Mac di Marco e sul branch `ralph/...`.
- Il diario delle sessioni di Ciani va in `docs/sessions/AAAA-MM-GG-ciani-NN.md`.
- Tutto il resto è in `docs/CONTRIBUTING.md`.
