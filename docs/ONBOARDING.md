# Onboarding for Ciani (and for their Claude)

## 1. Access
- Marco adds Ciani as a collaborator on the private repo `ZonatedCord/WatariBoot`, then Ciani accepts the invitation from email or from GitHub.

## 2. One-time setup
```sh
gh auth login                      # if not already done
gh repo clone ZonatedCord/WatariBoot ~/Documents/02_PROGETTI/WatariBoot
cd ~/Documents/02_PROGETTI/WatariBoot
./tools/install-hooks.sh           # blocks direct pushes to main
git config user.name "<First Last>"
git config user.email "<GitHub email>"
```

## 3. Configuring Claude Code on Ciani's Mac
The project rules live in the repo's `CLAUDE.md` and Claude reads them on its own. In addition, two settings are needed **on Ciani's machine**:
- Opus as the main model (`/model opus`).
- Sonnet as the default model for subagents. In `~/.claude/settings.json`:
  ```json
  { "env": { "CLAUDE_CODE_SUBAGENT_MODEL": "sonnet" } }
  ```
  Then Claude Code must be restarted. When the first agent starts, check in the panel that it really is Sonnet.

## 4. How Ciani works
- Ciani does **not** use Ralph: they work in normal sessions with their Claude (Opus boss + Sonnet subagents).
- They take tasks from the **GitHub Issues** by assigning them to themselves, and work on `ciani/<phase>-<topic>` branches. Every branch leads to a PR.
- They do not touch the files in `ralph/` nor the `ralph/...` branch.

## 5. Rules to remember
- Ralph, if used, runs only on Marco's Mac and on the `ralph/...` branch.
- Ciani's session diary goes in `docs/sessions/YYYY-MM-DD-ciani-NN.md`.
- Everything else is in `docs/CONTRIBUTING.md`.
