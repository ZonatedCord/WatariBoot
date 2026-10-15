# How to work as two people (plus Ralph) without conflicts

Three "authors" work on the same repo: **Marco**, **Ciani** and **Ralph** (Claude in an autonomous loop). These rules exist so that we do not step on each other's toes.

## 1. `main` is sacred
- Nobody pushes directly to `main`: everything goes through a **branch + Pull Request**, merged with **squash**.
- GitHub's free plan does not allow protecting branches on private repos, so the rule is enforced locally by a hook. After cloning, run once:
  ```sh
  ./tools/install-hooks.sh
  ```
  The `.githooks/pre-push` hook blocks pushes to `main`. For a real emergency: `WATARI_ALLOW_MAIN=1 git push`.
- If server-side protection is ever needed, there are two ways: GitHub Pro (about $4 a month) or making the repo public.

## 2. Branches
Format: `<author>/<phase>-<topic>`. Examples:
- `marco/f0-acpi-requisiti`
- `ciani/f1-qemu-baseline`
- `ralph/f0-gate-a`

Rules:
- One task = one branch = one PR. Small PRs, reviewable in under 15 minutes.
- Before starting: `git switch main && git pull --rebase && git switch -c <branch>`.
- Before opening the PR: `git fetch && git rebase origin/main`.
- Push often on your own branch.
- **Never work two people on the same branch.** This also applies to two Claude sessions.

## 3. Who does what: GitHub Issues
- Every task is an **issue**. Before starting it, assign it to yourself (`gh issue edit N --add-assignee @me`). An assigned issue is "taken".
- The PR closes the issue (`Closes #N` in the description).
- Exception: Ralph works from its own `ralph/TASKS-*.md` list, which only Ralph edits and only on its own branch.

## 4. "Hot" files (where conflicts are born) and how to handle them
| File | Rule |
|---|---|
| `docs/STATUS.md` | Edit it **only** in the PR that closes a milestone, or in the end-of-session PR. Never in PRs that do something else. |
| Session diary | One file per author and per session: `docs/sessions/YYYY-MM-DD-<author>-NN.md`. This way two people never write the same file. |
| ADRs | The draft is written as `docs/adr/draft-<title>.md`. **The number is assigned at merge** (the first free one), so there are no collisions. |
| `docs/PLAN.md` | Changes only together with an ADR. |
| `CLAUDE.md` | Changes only if both of you agree. |

## 5. Notifications
- Every issue, PR, comment or assignment sends an email to all collaborators. Work in **one PR per session or per milestone**, without unnecessary comments or reassignments.
- Suggestion: on the repo page, **Watch → Participating and @mentions**.

## 6. Review
- Every PR is approved by **someone who did not write it**. For Ralph's PRs: Opus does the technical check, then a human gives the final OK and does the merge.
- For code PRs: green CI (when it exists) + no Microsoft binaries (see `.gitignore`).

## 7. Commits
- Messages in English (from 2026-10-14; earlier history is in Italian), in the imperative mood, with the "why" when it is not obvious. Example: `acpigen: add MADT checksum`.
- The attribution lines for commits made by Claude follow the session's instructions.

## 8. Working with Claude Code
- [`CLAUDE.md`](../CLAUDE.md) at the repo root is loaded automatically by Claude Code, so every contributor's assistant follows the same workflow (Opus plans and verifies, Haiku scans, Sonnet edits), the same safety rules (backup before downloads, confirmation before irreversible actions) and the same documentation habits.
- Point the data directory to your own disk with `export WATARI_DATA=/path/to/WatariBoot-data` before running `tools/` scripts.
- Run `python3 -I tools/token-usage.py --autore <your-name>` at the end of a session to add your share to the cost table in the README.

## 9. Ralph
- It runs **only** on its own `ralph/...` branch, one at a time. It never merges.
- The operating rules are in `ralph/README.md`.
