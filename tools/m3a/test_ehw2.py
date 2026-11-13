#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fake-proxy test of ehw2.py. Run: python3 -I tools/m3a/test_ehw2.py   (no device, no m1n1 checkout).
Asserts: no write-type call is ever made (any other proxy attribute raises), the exception guard is set before the first read and
restored at exit, gated blocks are not read, a mismatching ADT address is skipped, faults are detected per read, SART entries are
intersected with the RAM list, dry-run makes no proxy call, and the source never imports m1n1.setup or touches NVRAM."""
import ast
import contextlib
import importlib.util
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ehw2", os.path.join(HERE, "ehw2.py"))
e2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e2)

FAILED = []


def check(c, m):
    print(("ok   " if c else "FAIL ") + m)
    if not c:
        FAILED.append(m)


class FakeProxy:
    """Only read32, read64, set_exc_guard, get_exc_count exist. Anything else (every write method included) raises."""

    def __init__(self, mem, fault_at=()):
        self.mem, self.fault_at, self.log, self.exc = mem, set(fault_at), [], 0

    def __getattr__(self, name):
        raise AssertionError("forbidden proxy call: " + name)

    def set_exc_guard(self, mode):
        self.log.append(("guard", mode))

    def get_exc_count(self):
        return self.exc

    def read32(self, a):
        self.log.append(("r", a))
        if a in self.fault_at:
            self.exc += 1
            return 0
        return self.mem.get(a, 0)

    read64 = read32


class Node:
    def __init__(self, path, regs, props=None):
        self._path, self._regs = path, regs
        self._properties = dict(props or {})
        self._properties["reg"] = regs

    def get_reg(self, i):
        return self._regs[i]


class FakeAdt:
    def __init__(self, nodes):
        self.nodes = {n._path: n for n in nodes}

    def __getitem__(self, p):
        return self.nodes[p]

    def walk_tree(self):
        return iter(self.nodes.values())


def make(ans2_active=True, ans_addr=0x277400000, fault=()):
    mem = {}
    ps = lambda v: v
    mem[e2.PMGR0 + 0x3f0] = 0xff if ans2_active else 0x00      # ACTUAL = 0xf / 0
    mem[e2.PMGR0 + 0x10018] = 0xff                              # disp0_cpu0 active
    mem[0x277400000 + 0x44] = 0x10                              # ANS CPU running
    mem[0x27bcc0000 + 0x1300] = 0xde71ce55
    sart = 0x27bc50000
    mem[sart + 0] = (0xff << 24) | 0x10                         # entry 0: 16 pages
    mem[sart + 0x40] = 0x802100000 >> 12                        # inside guest RAM
    mem[sart + 4] = (0xff << 24) | 0x4
    mem[sart + 0x44] = 0x9e0000000 >> 12                        # outside guest RAM
    nodes = [
        Node("/arm-io/pmgr", [(e2.PMGR0, 0x14000), (e2.PMGR1, 0x4000), (0x23b740000, 0x4000)]),
        Node("/arm-io/ans", [(ans_addr, 0x4000), (0, 0), (0, 0), (0x27bcc0000, 0x40000)]),
        Node("/arm-io/sart-ans", [(0x27bc50000, 0x10000)]),
        Node("/arm-io/dcp", [(0x231c00000, 0x4000)]),
        Node("/arm-io/smc", [(0x23e400000, 0x4000)]),
        Node("/chosen", [], {"unique-chip-id": b"SECRET-SERIAL", "mac-address": b"\x01\x02"}),
    ]
    return FakeProxy(mem, fault), FakeAdt(nodes)


def go(p, adt, ram=((0x802078000, 0x9df45c000),)):
    out = []
    lines = e2.run(p, adt, "SKIPSILENT", "OFF", list(ram), None)
    out.extend(lines)
    return "\n".join(out)


# 1. normal run: no forbidden call, guard order, restore
p, adt = make()
txt = go(p, adt)
check(p.log[0] == ("guard", "SKIPSILENT"), "guard set before the first read")
first_read = next(i for i, x in enumerate(p.log) if x[0] == "r")
check(first_read > 0, "first read comes after the guard")
check(p.log[-1] == ("guard", "OFF"), "guard restored to OFF at exit")
check("ANS ASC @0x277400000" in txt and "running" in txt, "ANS ASC read and decoded as running")
check("FIRMWARE UP" in txt, "ANS boot status decoded")
check("OVERLAPS guest RAM" in txt and "outside guest RAM" in txt, "SART entries intersected with the RAM list")
check("SECRET-SERIAL" not in txt and "unique-chip-id[" in txt, "/chosen listed by property name and length only")
check("SKIP SMC ASC" in txt, "SMC skipped: no known power domain")

# 2. gated block is not read
p, adt = make(ans2_active=False)
txt = go(p, adt)
reads = [x[1] for x in p.log if x[0] == "r"]
check(not any(0x277400000 <= a < 0x277400000 + 0x10000 for a in reads), "gated ANS ASC not read")
check(not any(0x27bc50000 <= a < 0x27bc60000 for a in reads), "gated SART not read")
check(not any(0x27bcc0000 <= a < 0x27bcc0000 + 0x40000 for a in reads), "gated ANS NVMe not read")
check("SKIP ANS ASC" in txt and "not ACTIVE" in txt, "skip reason printed")

# 3. ADT mismatch is skipped
p, adt = make(ans_addr=0x277000000)
txt = go(p, adt)
check("mismatch" in txt and "SKIP ANS ASC" in txt, "ADT/constant mismatch skipped")
check(not any(0x277000000 <= x[1] < 0x277010000 for x in p.log if x[0] == "r"), "mismatching base never read")

# 4. per-read fault detection
p, adt = make(fault=[0x277400000 + 0x44])
txt = go(p, adt)
check("FAULT" in txt and "1 faulted" in txt, "faulting read detected and reported")
check(p.log[-1] == ("guard", "OFF"), "guard restored after a fault")

# 5. dry run: no proxy at all
class NoProxy:
    def __getattr__(self, n):
        raise AssertionError("proxy touched in dry run: " + n)
buf = []
e2.dry_run(buf.append)
check(len(buf) > 20 and "DRY RUN" in buf[0], "dry run prints a plan")
with contextlib.redirect_stdout(io.StringIO()):
    rc = e2.main(["--dry-run"])
check(rc == 0, "main --dry-run returns 0 without importing m1n1")
check("m1n1" not in sys.modules, "m1n1 not imported in dry run")

# 6. source scan: no writes, no m1n1.setup, no NVRAM call
tree = ast.parse(open(os.path.join(HERE, "ehw2.py")).read())
bad = []
for n in ast.walk(tree):
    if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute):
        a = n.func.attr
        if a.startswith(("write", "set32", "clear32", "mask32", "memcpy", "memset", "nvram", "kboot", "reboot", "smp")):
            bad.append(a)
    if isinstance(n, (ast.Import, ast.ImportFrom)):
        mod = getattr(n, "module", None) or ""
        names = [x.name for x in n.names]
        if "setup" in mod.split(".") or "m1n1.setup" in names:
            bad.append("import " + mod)
check(not bad, "source: no write/NVRAM/setup calls %s" % bad)

# 7. PMGR device table parse on synthetic bytes (one device)
dev = bytearray(0x30 * 2)
for i, (id2, nm) in enumerate(((7, b"ANS2"), (9, b"DART"))):
    b = i * 0x30
    dev[b + 3] = 5          # id1 identical for both -> u16 ids are used
    dev[b + 0x0b], dev[b + 0x0c] = 2, 0
    dev[b + 0x1c:b + 0x1e] = id2.to_bytes(2, "little")
    dev[b + 0x20:b + 0x20 + len(nm)] = nm
ps = (0).to_bytes(4, "little") + (0x3f0).to_bytes(4, "little") + (0).to_bytes(4, "little")
adt2 = FakeAdt([Node("/arm-io/pmgr", [(e2.PMGR0, 0x14000), (e2.PMGR1, 0x4000), (0, 0)], {"devices": bytes(dev), "ps-regs": ps})])
d = e2.pmgr_devices(adt2, lambda s: None)
check(d.get(7) == ("ANS2", e2.PMGR0 + 0x3f0 + 16), "PMGR device table parsed: id 7 -> %s" % (d.get(7),))

print("\n%s: %d failure(s)" % ("FAIL" if FAILED else "PASS", len(FAILED)))
sys.exit(1 if FAILED else 0)
