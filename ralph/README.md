# Ralph loop: how to launch it

Ralph is Claude in a loop: at each iteration it receives the same prompt (`PROMPT-phase0.md`), performs **one** task from `TASKS-phase0.md`, verifies, commits on its own branch and starts again. It stops when it writes `<promise>RALPH STOP</promise>` or at the maximum number of iterations.

## Before launching it (checklist)
1. `./tools/install-hooks.sh` (once per clone): blocks direct pushes to `main`.
2. Up-to-date repo and dedicated branch:
   ```sh
   git switch main && git pull --rebase
   git switch -c ralph/f0-gate-a   # or: git switch ralph/f0-gate-a, if it already exists
   git push -u origin ralph/f0-gate-a
   ```
3. Start Claude Code **in the repo folder**, with Opus as the main model. `CLAUDE_CODE_SUBAGENT_MODEL=sonnet` is already set in `~/.claude/settings.json`. When the first agent starts, check in the panel that it really is Sonnet.
4. Nobody else (neither the partner nor another Claude session) must work on the `ralph/f0-gate-a` branch.

## Command
```
/ralph-loop "Run the instructions in ralph/PROMPT-phase0.md" --completion-promise "RALPH STOP" --max-iterations 15
```
- 7 tasks, one per iteration, plus margin for repeated attempts: 15 iterations are enough. Every iteration costs tokens: **do not remove `--max-iterations`.**
- To stop it at any time: `/cancel-ralph`.

## While it runs
- Progress can be read in `ralph/PROGRESS-phase0.md`, also from GitHub, because Ralph pushes after every task.
- Gate B (downloads, QEMU, Windows ISO) it does **not** do: it requires a confirmed backup, at least 100 GB free and human approval.

## When it is done
Ralph opens a PR `ralph/f0-gate-a → main`. A session with Opus is opened, which reviews the PR. Then a human approves and merges (squash).
