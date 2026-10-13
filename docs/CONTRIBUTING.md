# Come si lavora in due (più Ralph) senza conflitti

Tre "autori" lavorano sulla stessa repo: **Marco**, **Ciani** e **Ralph** (Claude in un loop autonomo). Queste regole servono a non pestarsi i piedi.

## 1. `main` è sacro
- Su `main` non si fa mai push diretto: tutto passa da **branch + Pull Request**, con merge **squash**.
- Il piano gratuito di GitHub non permette di proteggere i branch delle repo private, quindi la regola è garantita in locale da un hook. Dopo il clone si esegue una volta:
  ```sh
  ./tools/install-hooks.sh
  ```
  L'hook `.githooks/pre-push` blocca i push su `main`. Per un'emergenza vera: `WATARI_ALLOW_MAIN=1 git push`.
- Se un giorno serve una protezione lato server, ci sono due strade: GitHub Pro (circa 4 $ al mese) o rendere pubblica la repo.

## 2. Branch
Formato: `<autore>/<fase>-<tema>`. Esempi:
- `marco/f0-acpi-requisiti`
- `ciani/f1-qemu-baseline`
- `ralph/f0-gate-a`

Regole:
- Un task = un branch = una PR. PR piccole, che si rivedono in meno di 15 minuti.
- Prima di cominciare: `git switch main && git pull --rebase && git switch -c <branch>`.
- Prima di aprire la PR: `git fetch && git rebase origin/main`.
- Sul proprio branch si fa push spesso.
- **Non si lavora mai in due sullo stesso branch.** Vale anche per due sessioni Claude.

## 3. Chi fa cosa: GitHub Issues
- Ogni task è una **issue**. Prima di iniziarla te la assegni (`gh issue edit N --add-assignee @me`). Una issue assegnata è "presa".
- La PR chiude la issue (`Closes #N` nella descrizione).
- Eccezione: Ralph lavora sulla sua lista `ralph/TASKS-*.md`, che modifica solo lui e solo sul suo branch.

## 4. I file "caldi" (dove nascono i conflitti) e come gestirli
| File | Regola |
|---|---|
| `docs/STATUS.md` | Si modifica **solo** nella PR che chiude un traguardo, oppure nella PR di fine sessione. Mai in PR che fanno altro. |
| Diario delle sessioni | Un file per autore e per sessione: `docs/sessions/AAAA-MM-GG-<autore>-NN.md`. Così due persone non scrivono mai lo stesso file. |
| ADR | La bozza si scrive come `docs/adr/bozza-<titolo>.md`. **Il numero si assegna al merge** (il primo libero), così non ci sono collisioni. |
| `docs/PLAN.md` | Si cambia solo insieme a un ADR. |
| `CLAUDE.md` | Si cambia solo se siete d'accordo tutti e due. |

## 5. Notifiche
- Ogni issue, PR, commento o assegnazione manda una mail a tutti i collaboratori. Si lavora a **una PR per sessione o per traguardo**, senza commenti o riassegnazioni superflue.
- Consiglio: sulla pagina della repo, **Watch → Participating and @mentions**.

## 6. Review
- Ogni PR la approva **chi non l'ha scritta**. Per le PR di Ralph: Opus fa la verifica tecnica, poi un umano dà l'ok finale e fa il merge.
- Per le PR di codice: CI verde (quando ci sarà) + niente binari Microsoft (vedi `.gitignore`).

## 7. Commit
- Messaggi in italiano, all'imperativo, con il "perché" quando non è ovvio. Esempio: `acpigen: aggiungi checksum MADT`.
- Le righe di attribuzione per i commit fatti da Claude seguono le istruzioni della sessione.

## 8. Ralph
- Gira **solo** sul suo branch `ralph/...`, uno alla volta. Non fa mai merge.
- Le regole operative sono in `ralph/README.md`.
