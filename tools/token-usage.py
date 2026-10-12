#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Contatore dei token e stima dei costi del progetto Watari Boot.

Legge i transcript locali di Claude Code (~/.claude/projects/*), relativi a questo
progetto e ai suoi subagenti, e aggiorna:
  - docs/costs/<autore>.json: registro per sessione dell'autore (si può rieseguire: le sessioni già contate vengono sovrascritte, non sommate due volte)
  - README.md: la tabella tra i marcatori <!-- TOKEN-COUNTER:START/END -->, con il totale di tutti gli autori

Uso (a fine sessione):  python3 -I tools/token-usage.py --autore marco
"""
import argparse, collections, glob, json, os, pathlib, sys, datetime

# Prezzi in $ per milione di token (API Anthropic, listino al 2026-10-06; fonte: skill claude-api).
# Scrittura in cache: 1,25x l'input per TTL 5 minuti, 2x per TTL 1 ora (regola generale Anthropic: ipotesi da verificare se cambia il listino).
PRICES = {
    "claude-opus-5-5":   {"in": 4.00, "out": 20.00, "cache_read": 0.20},
    "claude-sonnet-5-5": {"in": 2.00, "out": 10.00, "cache_read": 0.20},
    "claude-haiku-5-5":  {"in": 0.10, "out": 0.50,  "cache_read": 0.01},
    "claude-fable-5-1":  {"in": 10.00, "out": 50.00, "cache_read": 0.25},
}
PROJECT_MARKERS = ("WatariBoot", "ProvaImpossibile")
FIELDS = ("input", "output", "cache_write_5m", "cache_write_1h", "cache_read")
REPO = pathlib.Path(__file__).resolve().parent.parent


def usage_of(u):
    cc = u.get("cache_creation") or {}
    w5 = cc.get("ephemeral_5m_input_tokens")
    w1 = cc.get("ephemeral_1h_input_tokens")
    total_w = u.get("cache_creation_input_tokens", 0) or 0
    if w5 is None and w1 is None:  # nessun dettaglio: si assume TTL 1h (quello usato da Claude Code)
        w5, w1 = 0, total_w
    return {"input": u.get("input_tokens", 0) or 0, "output": u.get("output_tokens", 0) or 0,
            "cache_write_5m": w5 or 0, "cache_write_1h": w1 or 0,
            "cache_read": u.get("cache_read_input_tokens", 0) or 0}


def scan_session(paths):
    """Totali per modello per una sessione (transcript principale + subagenti)."""
    per_model = collections.defaultdict(collections.Counter)
    for p in paths:
        per_msg = {}
        with open(p, encoding="utf-8") as fh:
            for line in fh:
                try:
                    o = json.loads(line)
                except ValueError:
                    continue
                m = o.get("message")
                if not isinstance(m, dict) or not m.get("usage"):
                    continue
                key = m.get("id") or id(o)
                model, cur = per_msg.setdefault(key, (m.get("model", "?"), collections.Counter()))
                for k, v in usage_of(m["usage"]).items():  # i messaggi in streaming si ripetono: si tiene il massimo
                    cur[k] = max(cur[k], v)
        for model, c in per_msg.values():
            per_model[model].update(c)
    return per_model


def cost(model, c):
    p = PRICES.get(model)
    if not p:
        return None
    return (c["input"] * p["in"] + c["output"] * p["out"] + c["cache_write_5m"] * p["in"] * 1.25
            + c["cache_write_1h"] * p["in"] * 2 + c["cache_read"] * p["cache_read"]) / 1e6


def find_sessions():
    base = pathlib.Path.home() / ".claude" / "projects"
    sessions = {}
    for d in base.iterdir() if base.exists() else []:
        if not any(mk in d.name for mk in PROJECT_MARKERS):
            continue
        for main in d.glob("*.jsonl"):
            sid = main.stem
            subs = list((d / sid).glob("**/*.jsonl")) if (d / sid).is_dir() else []
            sessions[sid] = [main] + subs
    return sessions


def render(ledgers):
    tot_model = collections.defaultdict(collections.Counter)
    tot_cost = collections.Counter()
    nsess = collections.Counter()
    for autore, led in ledgers.items():
        for s in led["sessions"].values():
            nsess[autore] += 1
            for model, c in s["models"].items():
                tot_model[model].update(c)
                tot_cost[autore] += cost(model, c) or 0
    rows = ["| Modello | Input | Output | Cache scritta | Cache letta | Costo stimato |", "|---|---:|---:|---:|---:|---:|"]
    grand = 0.0
    for model, c in sorted(tot_model.items()):
        k = cost(model, c)
        grand += k or 0
        rows.append(f"| `{model}` | {c['input']:,} | {c['output']:,} | {c['cache_write_5m'] + c['cache_write_1h']:,} | "
                    f"{c['cache_read']:,} | {'$%.2f' % k if k is not None else 'n/d'} |")
    tokens = sum(sum(c.values()) for c in tot_model.values())
    per_aut = ", ".join(f"{a}: {nsess[a]} sessioni, ${tot_cost[a]:.2f}" for a in sorted(ledgers))
    today = datetime.date.today().isoformat()
    return "\n".join([
        f"**Token totali: {tokens:,} · Costo stimato: ${grand:.2f}** (aggiornato {today}; {per_aut})", "",
        *rows, "",
        "<sub>Stima a prezzi API Anthropic, calcolata dai transcript locali di Claude Code con `tools/token-usage.py`. "
        "Con un abbonamento (Pro/Max) la spesa reale è il canone: questo è il costo equivalente a consumo. "
        "L'output dei subagenti può essere sottostimato nei log.</sub>"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--autore", required=True, help="es. marco, ciani")
    ap.add_argument("--no-readme", action="store_true")
    a = ap.parse_args()
    costs_dir = REPO / "docs" / "costs"
    costs_dir.mkdir(parents=True, exist_ok=True)
    led_path = costs_dir / f"{a.autore}.json"
    led = json.loads(led_path.read_text()) if led_path.exists() else {"autore": a.autore, "sessions": {}}
    for sid, paths in find_sessions().items():
        models = scan_session(paths)
        if models:
            led["sessions"][sid] = {"models": {m: dict(c) for m, c in models.items()}}
    led_path.write_text(json.dumps(led, indent=2, sort_keys=True) + "\n")
    ledgers = {p.stem: json.loads(p.read_text()) for p in costs_dir.glob("*.json")}
    table = render(ledgers)
    print(table)
    if not a.no_readme:
        r = REPO / "README.md"
        s = r.read_text()
        start, end = "<!-- TOKEN-COUNTER:START -->", "<!-- TOKEN-COUNTER:END -->"
        if start not in s:
            sys.exit("Marcatori TOKEN-COUNTER mancanti nel README")
        pre, rest = s.split(start, 1)
        post = rest.split(end, 1)[1]
        r.write_text(f"{pre}{start}\n{table}\n{end}{post}")


if __name__ == "__main__":
    main()
