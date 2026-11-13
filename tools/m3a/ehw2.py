#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""E-HW2: READ-ONLY inventory of firmware-initialised state over the m1n1 proxy (re/m3b-inherited-state.md section 5, T18b).

What it does: PMGR power states, ASC (coprocessor) control/status, SART entries, DART stream state, DWC3/ATC words, ADT facts.
What it never does: any write (no write*, set32, clear32, mask32, memcpy...), no NVRAM access of any kind, no import of m1n1.setup
(that module writes the PMU), no chainload, no SMP start. The only proxy calls are read32, read64, set_exc_guard and get_exc_count.
Rules: a block is read only if the PS register of its power domain says ACTUAL == 0xf; the exception guard is set before the first read,
the exception count is checked after every read, and the guard is restored to OFF at exit. Every address is derived from the ADT and
checked against the constant from the public sources; a mismatch or a missing ADT entry means SKIP (never a guess).
Caveat: this runs while m1n1 serves the proxy, i.e. BEFORE kboot_boot and the shutdowns of m1n1_main's tail: it is not the final
hand-over state (DWC3 of the console port is running, pcie_init has not run, display/mmu shutdown not yet done).
The report carries no identifying data: /chosen and /options are listed by property NAME and length only, never by value.

Usage: python3 ehw2.py [--out FILE] [--ram START:END ...] ; python3 ehw2.py --dry-run   (dry run: plan only, no proxy at all)
"""
import argparse
import struct
import sys

# ---- constants from the public sources (see the table in re/m3b-inherited-state.md section 5) ----
PMGR0 = 0x23b700000          # main PMGR (linux t8103.dtsi:547-552)
PMGR1 = 0x23d280000          # mini PMGR (linux t8103.dtsi:629)
PS_ACTIVE = 0xf

# name -> (pmgr 0/1, offset)   linux t8103-pmgr.dtsi
PS = {
    "ans2": (0, 0x3f0), "apcie_st": (0, 0x418), "apcie": (0, 0x348), "apcie_gp": (0, 0x3e8),
    "disp0_fe": (0, 0x360), "disp0_cpu0": (0, 0x10018), "dispdfr_fe": (0, 0x3a8), "dispdfr_be": (0, 0x3b0),
    "sep": (0, 0xc00), "pmp": (0, 0x3d8), "sio_busif": (0, 0x1c0), "sio": (0, 0x1c8), "sio_cpu": (0, 0x1d0),
    "fpwm1": (0, 0x1e0), "i2c0": (0, 0x1f0), "spi_p": (0, 0x218), "spi0": (0, 0x240), "spi3": (0, 0x258),
    "gpio": (0, 0x130), "mcc": (0, 0x2f8),
    "atc0_common": (0, 0x420), "atc0_pcie": (0, 0x428), "atc0_cio": (0, 0x430), "atc0_cio_pcie": (0, 0x438),
    "atc0_cio_usb": (0, 0x440), "atc1_common": (0, 0x448), "atc1_pcie": (0, 0x450), "atc1_cio": (0, 0x458),
    "atc1_cio_pcie": (0, 0x460), "atc1_cio_usb": (0, 0x468),
    "atc0_usb_aon": (1, 0x88), "atc1_usb_aon": (1, 0x90), "atc0_usb": (1, 0x98), "atc1_usb": (1, 0xa0), "debug_usb": (1, 0xb8),
}

# generic PS register ranges of the proxyclient PMGR map (hw/pmgr.py:25-50), bounded by the ADT reg size at run time
PS_RANGES = {
    0: [(0x0, 10), (0x200, 32), (0x300, 32), (0xc00, 2), (0x4000, 13), (0x8000, 5), (0xc000, 7), (0x10000, 10), (0x100, 32), (0x400, 15)],
    1: [(0x58, 32), (0x4000, 32), (0x8000, 32)],
}
CLK_RANGES = [(0x40000, 86), (0x40200, 8), (0x40280, 2)]    # relative to PMGR ADT reg 2

ASC_REGS = [(0x44, "CPU_CONTROL"), (0x48, "CPU_STATUS"), (0x8110, "MBOX_A2I_CTRL"), (0x8114, "MBOX_I2A_CTRL")]

# (label, adt_path or None, reg index, expected address, gate PS name or None, kind)
ASCS = [
    ("ANS ASC", "/arm-io/ans", 0, 0x277400000, "ans2"),
    ("DCP ASC", "/arm-io/dcp", 0, 0x231c00000, "disp0_cpu0"),
    ("SEP ASC", "/arm-io/sep", 0, 0x242400000, "sep"),
    ("SMC ASC", "/arm-io/smc", 0, 0x23e400000, None),
    ("AOP ASC", "/arm-io/aop", 0, 0x24a400000, None),
    ("PMP ASC", "/arm-io/pmp", 0, None, "pmp"),
]
NVME = ("ANS NVMe", "/arm-io/ans", 3, 0x27bcc0000, "ans2")
SART = ("SART ans", "/arm-io/sart-ans", 0, 0x27bc50000, "ans2")
DARTS = [
    ("dart-disp0", "/arm-io/dart-disp0", 0, 0x231304000, "disp0_cpu0"),
    ("dart-dcp", "/arm-io/dart-dcp", 0, 0x23130c000, "disp0_cpu0"),
    ("dart-usb0 a", None, 0, 0x382f00000, "atc0_usb"), ("dart-usb0 b", None, 0, 0x382f80000, "atc0_usb"),
    ("dart-usb1 a", None, 0, 0x502f00000, "atc1_usb"), ("dart-usb1 b", None, 0, 0x502f80000, "atc1_usb"),
    ("dart-apcie0", None, 0, 0x681008000, "apcie_gp"), ("dart-apcie1", None, 0, 0x682008000, "apcie_gp"),
    ("dart-apcie2", None, 0, 0x683008000, "apcie_gp"),
    ("sio-dart", None, 0, 0x235004000, "sio_cpu"),
]
DWC3 = [("DWC3 0", "/arm-io/usb-drd0", 0, 0x382280000, "atc0_usb"), ("DWC3 1", "/arm-io/usb-drd1", 0, 0x502280000, "atc1_usb")]
PHY = [("ATC PHY 0", "/arm-io/atc-phy0", 0, 0x383000000, "atc0_usb"), ("ATC PHY 1", "/arm-io/atc-phy1", 0, 0x503000000, "atc1_usb")]
PIPE = [("Pipehandler 0", "/arm-io/usb-drd0", 3, 0x382a84000, "atc0_usb"), ("Pipehandler 1", "/arm-io/usb-drd1", 3, None, "atc1_usb")]
ADT_FACT_PATHS = ["/arm-io/ans", "/arm-io/sart-ans", "/arm-io/dcp", "/arm-io/disp0", "/arm-io/dart-disp0", "/arm-io/dart-dcp",
                  "/arm-io/usb-drd0", "/arm-io/usb-drd1", "/arm-io/atc-phy0", "/arm-io/atc-phy1", "/arm-io/smc", "/arm-io/sep",
                  "/arm-io/spi3", "/arm-io/pmgr"]
DEFAULT_RAM = [(0x802078000, 0x9df45c000)]    # guest RAM of the day-2 L0 run (re/m3a-day2-results.md)


def decode_ps(v):
    t = {0xf: "ACTIVE", 0x4: "CLKGATED", 0x0: "PWRGATED"}.get((v >> 4) & 0xf, "other")
    fl = [n for b, n in ((31, "RESET"), (28, "AUTO_EN"), (11, "PARENT_OFF"), (10, "DEV_DISABLE"), (9, "WAS_CLKGATED"), (8, "WAS_PWRGATED")) if v >> b & 1]
    return "actual=0x%x(%s) target=0x%x %s" % ((v >> 4) & 0xf, t, v & 0xf, " ".join(fl))


class Reader:
    """The only object that touches the proxy. Methods: read32/read64 + guard handling. Faulting reads return None."""

    def __init__(self, p, guard_skip_silent):
        self.p, self.faults, self.reads = p, 0, 0
        self.p.set_exc_guard(guard_skip_silent)            # BEFORE the first read
        self.last = self.p.get_exc_count()

    def _chk(self, v):
        self.reads += 1
        c = self.p.get_exc_count()
        if c != self.last:
            self.last, self.faults = c, self.faults + 1
            return None
        return v

    def r32(self, a):
        return self._chk(self.p.read32(a))

    def r64(self, a):
        return self._chk(self.p.read64(a))

    def restore(self, guard_off):
        self.p.set_exc_guard(guard_off)


def adt_index(adt):
    """{address: path} of every reg entry in the ADT (translated), for address-based cross-checks."""
    idx = {}
    for n in adt.walk_tree():
        try:
            regs = n._properties.get("reg", None)
            if not regs or not hasattr(regs, "__len__"):
                continue
            for i in range(len(regs)):
                try:
                    a, _s = n.get_reg(i)
                except Exception:
                    continue
                if isinstance(a, int):
                    idx.setdefault(a, "%s reg[%d]" % (getattr(n, "_path", "?"), i))
        except Exception:
            continue
    return idx


def resolve(adt, idx, name, path, reg, const, say):
    """Return the address to use, or None (SKIP). The ADT must agree with the constant."""
    if path is not None:
        try:
            a = adt[path].get_reg(reg)[0]
        except Exception:
            a = None
        if a is not None:
            if const is not None and a != const:
                say("  SKIP %s: ADT %s reg[%d] = 0x%x but the constant is 0x%x (mismatch, not guessing)" % (name, path, reg, a, const))
                return None
            return a
    if const is not None and const in idx:
        return const
    say("  SKIP %s: not in the ADT (path %s, constant %s)" % (name, path, "0x%x" % const if const else "none"))
    return None


def pmgr_devices(adt, say):
    """Best-effort parse of /arm-io/pmgr `devices` + `ps-regs` (m1n1 src/pmgr.c struct pmgr_device, 0x30 bytes).
    Returns {id: (name, gate_address)}; {} if anything looks different from what was read in the sources."""
    try:
        node = adt["/arm-io/pmgr"]
        props = node._properties
        dev, ps = bytes(props["devices"]), bytes(props["ps-regs"])
        regs = [node.get_reg(i)[0] for i in range(3)]
        n = len(dev) // 0x30
        u8id = n >= 2 and dev[3] != dev[0x30 + 3]
        out = {}
        for i in range(n):
            d = dev[i * 0x30:(i + 1) * 0x30]
            flags, id1, addr_off, psidx = d[0], d[3], d[0x0b], d[0x0c]
            id2 = struct.unpack_from("<H", d, 0x1c)[0]
            name = d[0x20:0x30].split(b"\0")[0].decode("ascii", "replace")
            if flags & 0x10 or psidx * 12 + 12 > len(ps):
                continue
            ridx, roff = struct.unpack_from("<II", ps, psidx * 12)
            out[id1 if u8id else id2] = (name, regs[ridx] + roff + (addr_off << 3))
        return out
    except Exception as e:
        say("  (PMGR device table not parsed: %s %s)" % (type(e).__name__, e))
        return {}


def gate_from_adt(adt, path, devs):
    """PS register address of the first clock-gate of an ADT node, or None."""
    try:
        cg = adt[path]._properties.get("clock-gates")
        if cg is None:
            return None
        cg = cg if hasattr(cg, "__len__") else [cg]
        ids = [int(x) & 0xffff for x in cg]
        return devs[ids[0]][1] if ids and ids[0] in devs else None
    except Exception:
        return None


def run(p, adt, guard_skip_silent, guard_off, ram, out, devs_parse=True):
    lines = []

    def say(s=""):
        lines.append(s)
        if out:
            out(s)

    rd = Reader(p, guard_skip_silent)
    try:
        say("== ehw2: read-only; guard SKIP|SILENT set before the first read; faults are counted per read ==")
        idx = adt_index(adt)
        bases = {}
        for k, (const, path) in {0: (PMGR0, "/arm-io/pmgr"), 1: (PMGR1, None)}.items():
            bases[k] = resolve(adt, idx, "PMGR%d" % k, path, 0, const, say)
        # --- PMGR ---
        say("== PMGR (always readable) ==")
        ps_val = {}
        for nm, (k, off) in PS.items():
            if bases[k] is None:
                continue
            v = rd.r32(bases[k] + off)
            ps_val[nm] = v
            say("  PS %-14s %s+0x%05x = %s" % (nm, "main" if k == 0 else "mini", off, "FAULT" if v is None else "0x%08x  %s" % (v, decode_ps(v))))
        for k in (0, 1):
            if bases[k] is None:
                continue
            try:
                size = adt["/arm-io/pmgr"].get_reg(k)[1]
            except Exception:
                size = 0
            for start, cnt in PS_RANGES[k]:
                if start + cnt * 8 > size:
                    continue
                vals = [rd.r32(bases[k] + start + 8 * i) for i in range(cnt)]
                say("  PMGR%d PS block +0x%x x%d: %s" % (k, start, cnt, " ".join("-" if v is None else "%02x" % (v & 0xff) for v in vals)))
        try:
            clk = adt["/arm-io/pmgr"].get_reg(2)[0]
            for start, cnt in CLK_RANGES:
                vals = [rd.r32(clk + start + 4 * i) for i in range(cnt)]
                say("  CLK_CFG (reg2)+0x%x x%d: %s" % (start, cnt, " ".join("-" if v is None else "%08x" % v for v in vals)))
        except Exception as e:
            say("  CLK_CFG skipped: %s" % e)
        devs = pmgr_devices(adt, say) if devs_parse else {}

        def gate_ok(name, gate):
            if gate is None:
                return False, "no known power domain"
            v = ps_val.get(gate)
            if v is None:
                return False, "gate %s unreadable" % gate
            if (v >> 4) & 0xf != PS_ACTIVE:
                return False, "gate %s not ACTIVE (0x%08x)" % (gate, v)
            return True, ""

        def block(spec, regs, extra_gate=None):
            name, path, reg, const, gate = spec
            base = resolve(adt, idx, name, path, reg, const, say)
            if base is None:
                return None, None
            if gate is None and extra_gate is not None:
                a = extra_gate(path)
                if a is not None:
                    v = rd.r32(a)
                    ok = v is not None and (v >> 4) & 0xf == PS_ACTIVE
                    if not ok:
                        say("  SKIP %s: ADT gate PS = %s" % (name, "FAULT" if v is None else "0x%08x" % v))
                        return None, None
                    return base, [(o, l, rd.r32(base + o)) for o, l in regs]
            ok, why = gate_ok(name, gate)
            if not ok:
                say("  SKIP %s @0x%x: %s" % (name, base, why))
                return None, None
            return base, [(o, l, rd.r32(base + o)) for o, l in regs]

        def show(name, base, vals, dec=None):
            say("  %s @0x%x" % (name, base))
            for o, l, v in vals:
                s = "FAULT" if v is None else "0x%08x" % v
                if v is not None and dec:
                    s += "  " + dec(o, v)
                say("    +0x%04x %-18s %s" % (o, l, s))

        # --- ASCs ---
        say("== ASC coprocessors (CPU_CONTROL bit4 = running; mailbox: only bit 17 EMPTY is reliable) ==")
        gfa = lambda path: gate_from_adt(adt, path, devs)
        for spec in ASCS:
            b, v = block(spec, ASC_REGS, extra_gate=gfa)
            if b is not None:
                show(spec[0], b, v, lambda o, x: ("running" if x >> 4 & 1 else "stopped") if o == 0x44 else ("EMPTY" if x >> 17 & 1 else "not-empty") if o in (0x8110, 0x8114) else "")
        say("== ANS NVMe region ==")
        b, v = block(NVME, [(0x1300, "BOOT_STATUS"), (0x1c, "CSTS")])
        if b is not None:
            show(NVME[0], b, v, lambda o, x: ("FIRMWARE UP" if x == 0xde71ce55 else "not up") if o == 0x1300 else "")
        # --- SART ---
        say("== SART (v2: CONFIG +4*i flags 31:24 size 23:0 x4KB; PADDR +0x40+4*i x4KB) and overlap with guest RAM ==")
        regs = [(4 * i, "CONFIG%d" % i) for i in range(16)] + [(0x40 + 4 * i, "PADDR%d" % i) for i in range(16)]
        b, v = block(SART, regs)
        if b is not None:
            m = {l: x for _o, l, x in v}
            for i in range(16):
                c, a = m["CONFIG%d" % i], m["PADDR%d" % i]
                if c is None or a is None:
                    say("    entry %2d: FAULT" % i)
                    continue
                if (c >> 24) & 0xff == 0:
                    continue
                start, size = a << 12, (c & 0xffffff) << 12
                hit = [r for r in ram if start < r[1] and start + size > r[0]]
                say("    entry %2d: flags 0x%02x 0x%x..0x%x (size 0x%x) %s" % (i, c >> 24, start, start + size, size, "OVERLAPS guest RAM %s" % ["0x%x..0x%x" % r for r in hit] if hit else "outside guest RAM"))
        # --- DARTs ---
        say("== DARTs (T8020 layout; nothing but plain reads) ==")
        dregs = [(0x00, "PARAMS1"), (0x04, "PARAMS2"), (0x40, "ERROR"), (0x60, "CONFIG"), (0xfc, "ENABLED_STREAMS")]
        dregs += [(0x100 + 4 * s, "TCR%d" % s) for s in range(16)] + [(0x200 + 16 * s, "TTBR%d.0" % s) for s in range(16)]

        def ddec(o, x):
            if 0x100 <= o < 0x140:
                return "translate=%d bypass_dart=%d bypass_dapf=%d" % (x >> 7 & 1, x >> 8 & 1, x >> 12 & 1)
            if o >= 0x200:
                return "valid=%d addr=0x%x" % (x >> 31, (x & 0x7fffffff) << 12)
            if o == 0x60:
                return "locked=%d" % (x >> 15 & 1)
            if o == 0x00:
                return "page_shift=%d" % (x >> 24 & 0xf)
            return ""
        for spec in DARTS:
            b, v = block(spec, dregs)
            if b is not None:
                show(spec[0], b, v, ddec)
        # --- USB ---
        say("== USB (as seen over the proxy: the console DWC3 is RUNNING in device mode here, unlike at the hand-over) ==")
        for spec in DWC3:
            b, v = block(spec, [(0xc110, "GCTL"), (0xc118, "GSTS"), (0xc704, "DCTL"), (0xc70c, "DSTS")])
            if b is not None:
                show(spec[0], b, v, lambda o, x: "PRTCAPDIR=%d" % (x >> 12 & 3) if o == 0xc110 else "CURMOD=%d" % (x & 3) if o == 0xc118 else "RUN_STOP=%d" % (x >> 31) if o == 0xc704 else "DEVCTRLHLT=%d" % (x >> 22 & 1))
        for spec in PHY:
            b, v = block(spec, [(0x00, "w00"), (0x04, "w04"), (0x08, "w08"), (0x1c, "w1c")])
            if b is not None:
                show(spec[0], b, v)
        for spec in PIPE:
            b, v = block(spec, [(0x0c, "MUX_CTRL"), (0x1c, "AON_GEN"), (0x20, "NONSELECTED_OVERRIDE")])
            if b is not None:
                show(spec[0], b, v)
        # --- ADT facts (names and lengths only for identifying nodes) ---
        say("== ADT facts (host side, no hardware access) ==")
        for pth in ADT_FACT_PATHS:
            try:
                n = adt[pth]
                regs = [("0x%x+0x%x" % n.get_reg(i)) for i in range(len(n._properties.get("reg", [])))]
                say("  %s reg=%s clock-gates=%s compatible=%s" % (pth, regs, n._properties.get("clock-gates"), n._properties.get("compatible")))
            except Exception as e:
                say("  %s: not available (%s)" % (pth, type(e).__name__))
        for pth in ("/options", "/chosen"):
            try:
                pr = adt[pth]._properties
                say("  %s property names (values NOT printed): %s" % (pth, ", ".join("%s[%s]" % (k, len(v) if hasattr(v, "__len__") else "int") for k, v in pr.items())))
            except Exception as e:
                say("  %s: not available (%s)" % (pth, type(e).__name__))
        try:
            mm = adt["/chosen/memory-map"]._properties
            for k, v in mm.items():
                say("  memory-map %s = %s" % (k, ["0x%x" % x if isinstance(x, int) else x for x in v] if hasattr(v, "__iter__") else v))
        except Exception as e:
            say("  /chosen/memory-map: not available (%s)" % type(e).__name__)
        say("done: %d reads, %d faulted" % (rd.reads, rd.faults))
    finally:
        rd.restore(guard_off)
    return lines


def dry_run(say):
    say("== ehw2 DRY RUN: nothing is sent to any proxy; this is the plan (constants only, the ADT would be consulted at run time) ==")
    say("proxy calls used: set_exc_guard(SKIP|SILENT) once, then read32/read64 each followed by get_exc_count, set_exc_guard(OFF) at exit")
    say("PMGR main 0x%x, mini 0x%x: named PS registers:" % (PMGR0, PMGR1))
    for nm, (k, off) in PS.items():
        say("  %-14s %s+0x%05x" % (nm, "main" if k == 0 else "mini", off))
    for spec in ASCS + [NVME, SART] + DARTS + DWC3 + PHY + PIPE:
        name, path, reg, const, gate = spec
        say("block %-14s adt=%s reg[%d] const=%s gate=%s" % (name, path, reg, "0x%x" % const if const else "-", gate or "ADT clock-gates or skip"))
    say("never read: NVRAM (not on the proxy), /options and /chosen values (names only)")


def main(argv=None):
    ap = argparse.ArgumentParser(description="E-HW2 read-only inventory over the m1n1 proxy")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, make no proxy call")
    ap.add_argument("--out", help="also write the report to this file")
    ap.add_argument("--ram", action="append", help="guest RAM range START:END (hex), repeatable; default: day-2 L0 range")
    a = ap.parse_args(argv)
    f = open(a.out, "w") if a.out else None

    def out(s):
        print(s, flush=True)
        if f:
            print(s, file=f, flush=True)      # host-side report file, not a proxy write
    if a.dry_run:
        dry_run(out)
        return 0
    ram = [tuple(int(x, 16) for x in r.split(":")) for r in a.ram] if a.ram else DEFAULT_RAM
    from m1n1.proxy import UartInterface, M1N1Proxy, GUARD             # never m1n1.setup
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p)
    run(p, u.adt, GUARD.SKIP | GUARD.SILENT, GUARD.OFF, ram, lambda s: (out(s)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
