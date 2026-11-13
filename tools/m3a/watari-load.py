#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""watari-load: host loader of the M3a hardware day (DESIGN 2.5, PLAN S17).

Runs on the HOST Mac (Ciani's), with the target Mac in m1n1 proxy mode. It uses the m1n1 proxyclient (GPL-2.0, NOT
vendored: the checkout of day 1, path from --m1n1 or env M1N1_DIR). It imports only m1n1.proxy, m1n1.proxyutils and
m1n1.sysreg. It NEVER imports m1n1.setup: setup.py resets a panic counter in the PMU (an SPMI write, F-29).

Call order of a run (the unit test test_watari_load.py asserts it):
  1. verify the bundle against SHA256SUMS (refuse otherwise)
  2. allocate + write: DTB (malloc), layer (memalign 32 MB), guest (memalign 32 MB, rungs L3-L5)
  3. cache maintenance: dc cvau + ic ivau over each image
  4. ONLY NOW set the boot arguments (they contain the guest address, DESIGN L-2): kboot_set_chosen("bootargs", ...)
  5. optional smp_start_secondaries (rung L5)
  6. kboot_prepare_dt(dtb)   (m1n1 fills the DTB from the ADT; a non-zero return aborts before anything runs)
  7. rung L0 stops here: read the prepared FDT back, save it in the host folder, run fdt_m1_dump on it. NO jump.
  8. DAIF = 0xc0, kboot_boot(layer). USB goes away during this request: a lost reply or a timeout AFTER the request was
     sent counts as "handed over".

The loader writes only RAM (heap blocks, the DTB, m1n1's FDT copy). m1n1's own kboot_prepare_dt / kboot_boot do
non-persistent hardware initialisation (ISP heap, display DART lock, USB/PCIe bring-up, SEP random bytes), all reset by a
reboot [hypothesis: not every helper was read]. Nothing is written to storage, NVRAM or the PMU by us.

Exit codes: 0 ok / handed over, 1 error, 2 usage or bundle problem, 3 L0: prepared but cannot read the FDT back, 4 L0: a heap block is not wholly inside usable RAM.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys

BLOCK = 32 * 1024 * 1024          # DESIGN D-2: one stage-2 block of the 16 KB granule

# rung -> (layer image, guest image or None, step or None, start secondaries)
RUNGS = {
    "L0": (None, None, None, True),
    "L1": ("wel2-m1.bin", None, 1, False),
    "L2": ("wel2-m1.bin", None, 2, False),
    "L3": ("wel2-chain.bin", "m1guest.bin", 3, False),
    "L4": ("wel2-chain.bin", "m1guest.bin", 4, False),
    "L5": ("wel2-chain.bin", "m1guest.bin", 5, True),
}


class LoadError(Exception):
    def __init__(self, msg, code=1):
        super().__init__(msg)
        self.code = code


# ----------------------------------------------------------------------------------------------------------------------
# Bundle
# ----------------------------------------------------------------------------------------------------------------------

def sha256(data):
    return hashlib.sha256(data).hexdigest()


def verify_bundle(bundle, required=()):
    """Check every line of SHA256SUMS, reading each file once (hash and content from the same bytes).
    `required` names must be listed. Returns {name: bytes}; raises LoadError on any problem."""
    sums = os.path.join(bundle, "SHA256SUMS")
    if not os.path.isfile(sums):
        raise LoadError("no SHA256SUMS in %s: not a bundle made by build-m1-payload.sh" % bundle, 2)
    loaded = {}
    with open(sums) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            m = re.match(r"^([0-9a-f]{64})\s+\*?(.+)$", line)
            if not m:
                raise LoadError("malformed line in SHA256SUMS: %r" % line, 2)
            want, name = m.group(1), m.group(2)
            path = os.path.join(bundle, name)
            if not os.path.isfile(path):
                raise LoadError("bundle file missing: %s" % name, 2)
            with open(path, "rb") as g:
                data = g.read()
            if sha256(data) != want:
                raise LoadError("SHA-256 mismatch for %s (bundle damaged or edited): refusing to run" % name, 2)
            loaded[name] = data
    for name in required:
        if name not in loaded:
            raise LoadError("%s is not listed in SHA256SUMS: refusing to use an unverified file" % name, 2)
    return loaded


GUEST_INFO_SIZE = 136      # sizeof(struct wel2_guest_info) (el2/include/wel2_guest_info.h: 8+4+4+16+3*8+4*4+8*8)
GUEST_ALIGN = 4096         # the layer puts the info block at the first 4 KB boundary above the image end (guest_place.c)


def check_image(name, data, guest=False):
    """arm64 Image header (DESIGN 2.4): magic at 0x38, image_size at 16; must fit one 32 MB block."""
    if len(data) < 64 or data[0x38:0x3c] != b"ARM\x64":
        raise LoadError("%s: no arm64 Image magic at offset 0x38" % name)
    image_size = struct.unpack_from("<Q", data, 16)[0]
    if len(data) > BLOCK or image_size > BLOCK:
        raise LoadError("%s: larger than the 32 MB block" % name)
    if image_size < len(data):
        raise LoadError("%s: header image_size smaller than the file" % name)
    if guest and image_size + GUEST_ALIGN + GUEST_INFO_SIZE > BLOCK:
        raise LoadError("%s: image_size + 4 KB + guest info block does not fit the 32 MB block" % name)


def check_dtb(data):
    if len(data) < 40 or data[:4] != b"\xd0\x0d\xfe\xed":
        raise LoadError("the DTB has no FDT magic")
    if struct.unpack_from(">I", data, 4)[0] > len(data):
        raise LoadError("the DTB is truncated (totalsize larger than the file)")


# ----------------------------------------------------------------------------------------------------------------------
# Boot arguments
# ----------------------------------------------------------------------------------------------------------------------

def build_bootargs(step, cpuif, guest_addr, extra):
    """wel2.* arguments (el2/boot/README.md). Built only after the guest block exists (its address is in them)."""
    parts = []
    if step is not None:
        parts.append("wel2.step=%d" % step)
    if step is not None and step >= 2 and cpuif:
        parts.append("wel2.cpuif=%s" % cpuif)
    if guest_addr is not None:
        parts.append("wel2.guest=0x%x" % guest_addr)
    if extra:
        parts.extend(extra.split())
    return " ".join(parts)


# ----------------------------------------------------------------------------------------------------------------------
# Targets: the only objects that talk to a device (real) or print and record (dry run / unit test)
# ----------------------------------------------------------------------------------------------------------------------

class DryTarget:
    """No device. Prints every proxy call and records it in self.calls. Also the base of the unit test's fake."""
    transport_errors = (OSError,)

    def __init__(self, echo=True):
        self.calls = []
        self.echo = echo
        self.mem = {}                       # addr -> bytes written
        self._next = 0x8_4000_0000
        self.request_sent = False
        self.fail_prepare = 0               # return value of prepare_dt
        self.boot_error = None              # exception raised by boot() (after "send" unless boot_error_before_send)
        self.boot_error_before_send = False
        self.fdt_addr = None
        self._console = ""

    def _log(self, name, *args):
        self.calls.append((name,) + args)
        if self.echo:
            print("  [proxy] %s(%s)" % (name, ", ".join(a if isinstance(a, str) else
                                                       (hex(a) if isinstance(a, int) else repr(a)) for a in args)))

    def set_timeout(self, sec):
        self._log("timeout", sec)

    def malloc(self, n):
        a = self._next
        self._next += (n + 0xffff) & ~0xffff
        self._log("malloc", n)
        return a

    def memalign(self, align, n):
        a = (self._next + align - 1) & ~(align - 1)
        self._next = a + n
        self._log("memalign", align, n)
        return a

    def write(self, addr, data):
        self.mem[addr] = bytes(data)
        self._log("writemem", addr, len(data))

    def read(self, addr, n):
        self._log("readmem", addr, n)
        for base, d in self.mem.items():
            if base <= addr < base + len(d):
                return d[addr - base:addr - base + n]
        return b"\0" * n

    def dc_cvau(self, addr, n):
        self._log("dc_cvau", addr, n)

    def ic_ivau(self, addr, n):
        self._log("ic_ivau", addr, n)

    def dc_cvac(self, addr, n):
        self._log("dc_cvac", addr, n)

    def cpufreq_init(self):
        self._log("cpufreq_init")

    def set_chosen(self, name, value):
        self._log("kboot_set_chosen", name, value)

    def smp_start_secondaries(self):
        self._log("smp_start_secondaries")

    def prepare_dt(self, addr):
        self._log("kboot_prepare_dt", addr)
        if self.fail_prepare:
            return self.fail_prepare
        # dry run: the "prepared" FDT is the DTB itself (labelled as such in the output)
        self.fdt_addr = addr
        self._console += "FDT prepared at 0x%x\n" % addr
        return 0

    def set_daif(self, v):
        self._log("msr_daif", v)

    def boot(self, addr):
        self._log("kboot_boot", addr)
        if self.boot_error is not None and self.boot_error_before_send:
            raise self.boot_error
        self.request_sent = True
        if self.boot_error is not None:
            raise self.boot_error

    def console_text(self):
        return self._console


class M1n1Target:
    """The real thing: m1n1 proxyclient objects (proxy, proxyutils). Built by connect()."""

    def __init__(self, iface, p, u, errors, daif_reg):
        self.iface, self.p, self.u = iface, p, u
        self.transport_errors = errors
        self.request_sent = False
        self._daif = daif_reg
        self._console = ""
        self._orig_cmd = iface.cmd
        iface.cmd = self._cmd                          # notice when a request leaves the host
        self._orig_unk = iface.unkhandler
        iface.unkhandler = self._unk                   # m1n1 console text arrives here while we wait for a reply

    def _cmd(self, *a, **k):
        self.request_sent = True
        return self._orig_cmd(*a, **k)

    def _unk(self, s):
        try:
            self._console += bytes(s).decode("utf-8", "replace")
        except Exception:
            pass

    def set_timeout(self, sec):
        self.iface.dev.timeout = sec

    def malloc(self, n):
        return self.u.malloc(n)

    def memalign(self, align, n):
        return self.u.memalign(align, n)

    def write(self, addr, data):
        self.iface.writemem(addr, data, True)

    def read(self, addr, n):
        return self.iface.readmem(addr, n)

    def dc_cvau(self, addr, n):
        self.p.dc_cvau(addr, n)

    def ic_ivau(self, addr, n):
        self.p.ic_ivau(addr, n)

    def dc_cvac(self, addr, n):
        self.p.dc_cvac(addr, n)

    def cpufreq_init(self):
        self.p.cpufreq_init()

    def set_chosen(self, name, value):
        self.p.kboot_set_chosen(name, value)

    def smp_start_secondaries(self):
        self.p.smp_start_secondaries()

    def prepare_dt(self, addr):
        return self.p.kboot_prepare_dt(addr)

    def set_daif(self, v):
        self.u.msr(self._daif, v)

    def boot(self, addr):
        self.request_sent = False
        self.p.kboot_boot(addr)

    def console_text(self):
        return self._console


def connect(m1n1_dir, device=None):
    """Open the proxy the way tools/m3a/ehw0.py does (read-only set-up). Returns an M1n1Target.
    Imports ONLY m1n1.proxy, m1n1.proxyutils and m1n1.sysreg; m1n1.setup is never imported."""
    root = os.path.abspath(os.path.expanduser(m1n1_dir))
    pc = root if os.path.isdir(os.path.join(root, "m1n1")) else os.path.join(root, "proxyclient")
    if not os.path.isfile(os.path.join(pc, "m1n1", "proxy.py")):
        raise LoadError("%s is not an m1n1 checkout (no m1n1/proxy.py; pass the checkout or its proxyclient dir)" % root)
    sys.path.insert(0, pc)
    if device:
        os.environ["M1N1DEVICE"] = device
    if "M1N1DEVICE" not in os.environ:
        raise LoadError("no device: set M1N1DEVICE=/dev/cu.usbmodem...1 (the proxy port) or pass --device")
    from m1n1.proxy import UartInterface, M1N1Proxy, UartError
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    from m1n1.sysreg import DAIF
    if "m1n1.setup" in sys.modules:
        raise LoadError("m1n1.setup got imported (it writes the PMU): refusing to continue")
    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p)                                   # heap set-up only (RAM)
    try:
        import serial
        errors = (UartError, OSError, serial.SerialException)
    except ImportError:
        errors = (UartError, OSError)
    return M1n1Target(iface, p, u, errors, DAIF)


# ----------------------------------------------------------------------------------------------------------------------
# The run
# ----------------------------------------------------------------------------------------------------------------------

def say(*a):
    print(*a, flush=True)


def find_fdt_addr(console, manual):
    if manual is not None:
        return manual
    m = None
    for m in re.finditer(r"FDT prepared at (?:0x)?([0-9a-fA-F]+)", console):
        pass
    return int(m.group(1), 16) if m else None


def parse_usable(dump_text):
    """('usable i base=0x.. size=0x..' lines of fdt_m1_dump) -> ([(base, size)], truncated)."""
    ranges = [(int(m.group(1), 16), int(m.group(2), 16))
              for m in re.finditer(r"^usable \d+ base=0x([0-9a-f]+) size=0x([0-9a-f]+)", dump_text, re.M)]
    return ranges, "WARNING: a list was truncated" in dump_text


def check_blocks(dump_text, blocks):
    """Is each block (name -> address) wholly inside ONE usable range (FDT RAM minus reservations)? The usable ranges are
    exact bytes; the layer rounds them inward to 32 MB, which keeps any 32 MB-aligned 32 MB block that fits. Returns
    (all_ok, report lines)."""
    usable, truncated = parse_usable(dump_text)
    lines, ok_all = [], True
    for name, addr in blocks.items():
        end = addr + BLOCK
        hit = [r for r in usable if r[0] <= addr and end <= r[0] + r[1]]
        ok = bool(hit) and addr % BLOCK == 0
        ok_all &= ok
        if ok:
            res = "PASS (inside usable range 0x%x..0x%x)" % (hit[0][0], hit[0][0] + hit[0][1])
        else:
            res = "FAIL (not wholly inside one usable range)"
        lines.append("  %-5s block 0x%x..0x%x : %s" % (name, addr, end, res))
    if truncated:
        lines.append("  WARNING: the dump says a list was truncated: the check is not complete")
        ok_all = False
    if not usable:
        lines.append("  no usable ranges in the dump")
        ok_all = False
    return ok_all, lines


def run_dump(dump_tool, blob_path):
    say("== fdt_m1_dump (the layer's own FDT queries, nothing else of the blob) ==")
    r = subprocess.run([dump_tool, blob_path], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        sys.stderr.write(r.stderr)
        raise LoadError("fdt_m1_dump failed with exit %d: the prepared FDT would not parse for the layer" % r.returncode)
    return r.stdout


def ensure_dump_tool(bundle, given):
    """Prefer the prebuilt arm64 fdt_m1_dump of the bundle (covered by SHA256SUMS). Never run `cc` blind: on a Mac
    without the Command Line Tools /usr/bin/cc can open the installer (an install on a zero-footprint host)."""
    for tool in (given, os.path.join(bundle, "fdt_m1_dump")):
        if not tool:
            continue
        if os.path.isfile(tool) and not os.access(tool, os.X_OK):
            os.chmod(tool, 0o755)
        if os.access(tool, os.X_OK):
            return tool
        if tool == given:
            raise LoadError("--fdt-dump %s is not usable" % given)
    build = os.path.join(bundle, "fdt-dump-src", "build.sh")
    if os.path.isfile(build) and subprocess.run(["xcode-select", "-p"], capture_output=True).returncode == 0:
        say("building fdt_m1_dump with the installed Command Line Tools (output stays in the bundle folder) ...")
        subprocess.run(["sh", build], check=True)
        built = os.path.join(bundle, "fdt-dump-src", "fdt_m1_dump")
        if os.access(built, os.X_OK):
            return built
    raise LoadError("no usable fdt_m1_dump (prebuilt one missing or not runnable, Command Line Tools not installed): "
                    "the prepared FDT is saved; check it on Marco's Mac with the same tool")


def run(args, target):
    """The whole sequence on `target`. Returns the process exit code."""
    layer_name, guest_name, step, smp = RUNGS[args.rung]
    bundle = args.bundle
    if args.smp is not None:
        smp = args.smp

    required = [x for x in (layer_name, guest_name) if x]
    if not args.dtb:
        required.append("j293-minimal.dtb")
    verified = verify_bundle(bundle, required)
    say("bundle OK: %d files match SHA256SUMS (%s)" % (len(verified), bundle))
    load = verified.__getitem__

    dtb_name = os.path.basename(args.dtb) if args.dtb else "j293-minimal.dtb"
    if args.dtb:
        with open(args.dtb, "rb") as f:
            dtb = f.read()
        say("NOTE: --dtb is outside SHA256SUMS; its hash is printed below.")
    else:
        dtb = load("j293-minimal.dtb")
    check_dtb(dtb)
    layer = guest = None
    if layer_name:
        layer = load(layer_name)
        check_image(layer_name, layer)
    if guest_name:
        guest = load(guest_name)
        check_image(guest_name, guest, guest=True)

    say("rung %s: layer=%s guest=%s step=%s cpuif=%s secondaries=%s" %
        (args.rung, layer_name or "-", guest_name or "-", step if step is not None else "-",
         args.cpuif if (step or 0) >= 2 else "-", smp))
    if args.rung != "L0":
        say("rung %s ends with a JUMP to the layer; the target leaves m1n1 (recovery: power button ~10 s)." % args.rung)

    # --- 2. allocate and write ---------------------------------------------------------------------------------------
    dtb_addr = target.malloc(len(dtb))
    target.write(dtb_addr, dtb)
    say("DTB    %-22s %7d bytes at 0x%x  sha256 %s" % (dtb_name, len(dtb), dtb_addr, sha256(dtb)[:16]))
    layer_addr = guest_addr = None
    l0_blocks = {}
    if args.rung == "L0":
        # L0 allocates the two blocks the jump rungs would use, WITHOUT writing them: the host then checks that m1n1's
        # proxy heap is plain usable RAM (the main unverified risk for L3).
        l0_blocks = {"layer": target.memalign(BLOCK, BLOCK), "guest": target.memalign(BLOCK, BLOCK)}
        for k, v in l0_blocks.items():
            say("BLOCK  %-5s 32 MB aligned block at 0x%x (not written)" % (k, v))
    if layer is not None:
        layer_addr = target.memalign(BLOCK, BLOCK)
        if layer_addr % BLOCK:
            raise LoadError("the layer block is not 32 MB aligned (0x%x)" % layer_addr)
        target.write(layer_addr, layer)
        say("LAYER  %-22s %7d bytes at 0x%x  sha256 %s" % (layer_name, len(layer), layer_addr, sha256(layer)[:16]))
    if guest is not None:
        guest_addr = target.memalign(BLOCK, BLOCK)
        if guest_addr % BLOCK:
            raise LoadError("the guest block is not 32 MB aligned (0x%x)" % guest_addr)
        target.write(guest_addr, guest)
        say("GUEST  %-22s %7d bytes at 0x%x  sha256 %s" % (guest_name, len(guest), guest_addr, sha256(guest)[:16]))

    # --- 3. cache maintenance ----------------------------------------------------------------------------------------
    for addr, data in ((layer_addr, layer), (guest_addr, guest)):
        if data is not None:
            target.dc_cvau(addr, len(data))
            target.dc_cvac(addr, len(data))      # to the point of coherency; the real guarantee is m1n1's set/way flush in mmu_shutdown
            target.ic_ivau(addr, len(data))

    # --- 4. boot arguments, after the allocations --------------------------------------------------------------------
    bootargs = build_bootargs(step, args.cpuif, guest_addr, args.args)
    if bootargs:
        say('boot arguments: "%s"' % bootargs)
        target.set_chosen("bootargs", bootargs)
    else:
        say("boot arguments: none (rung L0)")

    # --- 5. secondaries ----------------------------------------------------------------------------------------------
    if smp:
        # linux.py calls cpufreq_init() before smp_start_secondaries() [check: needed here]. Without alive secondaries
        # m1n1's dt_set_cpus removes their nodes: the FDT would have 1 cpu and never validate the 8-cpu parse.
        target.cpufreq_init()
        target.smp_start_secondaries()

    # --- 6. m1n1 prepares the FDT ------------------------------------------------------------------------------------
    target.set_timeout(40)
    rc = target.prepare_dt(dtb_addr)
    if rc:
        say("kboot_prepare_dt FAILED (returned %d): nothing of ours has run; the proxy is alive." % rc)
        say("Try the fallback DTB: --dtb <upstream t8103-j293.dtb> (README-m1-payload.md, 'Fallback DTB').")
        return 1
    say("kboot_prepare_dt OK")
    console = target.console_text()
    if console:
        os.makedirs(args.out, exist_ok=True)
        with open(os.path.join(args.out, "m1n1-console.log"), "w") as f:
            f.write(console)          # host folder only; may hold board identifiers: never commit, never paste
        for line in console.splitlines():
            if re.search(r"FDT prepared|couldn't|not found|failed|unavailable|unsupported|skipping", line, re.I):
                say("  m1n1: " + line.strip())
    fdt_addr = find_fdt_addr(console, args.fdt_addr)

    # --- 7. rung L0: read it back, check it, stop --------------------------------------------------------------------
    if args.rung == "L0":
        if fdt_addr is None:
            say("L0: the prepared FDT address could not be captured from m1n1's console text.")
            say("Read it from the m1n1 console (line 'FDT prepared at 0x...') and rerun with --fdt-addr 0x...")
            return 3
        hdr = target.read(fdt_addr, 40)
        if hdr[:4] != b"\xd0\x0d\xfe\xed":
            say("L0: no FDT magic at 0x%x (wrong address?)" % fdt_addr)
            return 3
        total = struct.unpack_from(">I", hdr, 4)[0]
        if not 40 <= total <= 4 * 1024 * 1024:
            say("L0: implausible totalsize %d in the FDT header" % total)
            return 3
        blob = target.read(fdt_addr, total)
        os.makedirs(args.out, exist_ok=True)
        path = os.path.join(args.out, "prepared-fdt.dtb")
        with open(path, "wb") as f:
            f.write(blob)
        say("prepared FDT: %d bytes at 0x%x, saved to %s (host folder only: it holds the board serial number;" %
            (total, fdt_addr, path))
        say("never commit it, never paste it). sha256 %s" % sha256(blob)[:16])
        if isinstance(target, DryTarget):
            say("(dry run: the 'prepared' FDT is the unmodified DTB; on hardware m1n1 adds memory, cpus, framebuffer)")
        dump_text = run_dump(ensure_dump_tool(bundle, args.fdt_dump), path)
        say("== block placement check (host side): are the layer/guest blocks wholly inside usable RAM? ==")
        ok, lines = check_blocks(dump_text, l0_blocks)
        for ln in lines:
            say(ln)
        if isinstance(target, DryTarget):
            say("(dry run: the fake addresses are not m1n1's heap; this only exercises the check)")
        if not ok:
            say("BLOCK CHECK: FAIL - the heap blocks are not plain usable RAM in this FDT. Do NOT run L3-L5: the layer "
                "would refuse the guest. L1/L2 do not need the guest block but still use the layer block.")
            return 4
        say("BLOCK CHECK: PASS")
        say("L0 done: no jump was made; m1n1 is still alive on the proxy.")
        return 0

    # --- 8. the jump -------------------------------------------------------------------------------------------------
    target.set_timeout(args.boot_timeout)
    target.set_daif(0xc0)
    say("kboot_boot(0x%x) ... (USB goes away now; a timeout is expected)" % layer_addr)
    try:
        target.boot(layer_addr)
    except target.transport_errors as e:
        if not target.request_sent:
            say("kboot_boot: the request was NOT sent (%s: %s). Nothing was handed over." % (type(e).__name__, e))
            return 1
        say("kboot_boot: no reply (%s: %s). Usually the USB link going away; the screen tells." % (type(e).__name__, e))
    except Exception as e:
        say("kboot_boot: unexpected error %s: %s. Not treated as a hand-over." % (type(e).__name__, e))
        return 1
    say("handed over. Watch the screen (README-m1-payload.md); L1: the Mac restarts within about a second.")
    return 0


def parse_args(argv):
    ap = argparse.ArgumentParser(description="Watari EL2 host loader (M3a). Writes on the target are RAM only.")
    ap.add_argument("--rung", required=True, choices=sorted(RUNGS), help="L0 = prepare the DT only, no jump")
    ap.add_argument("--m1n1", default=os.environ.get("M1N1_DIR"), help="m1n1 checkout or its proxyclient dir (env M1N1_DIR)")
    ap.add_argument("--device", help="proxy serial device (default: env M1N1DEVICE)")
    ap.add_argument("--bundle", default=os.path.dirname(os.path.abspath(__file__)), help="bundle folder (default: this script's)")
    ap.add_argument("--out", help="where the prepared FDT and the console log are kept (default: <bundle>/out)")
    ap.add_argument("--dtb", help="fallback DTB instead of the bundle's j293-minimal.dtb (e.g. upstream t8103-j293.dtb)")
    ap.add_argument("--cpuif", choices=["trap", "trap-notc"], default="trap", help="wel2.cpuif for rungs L2-L5")
    ap.add_argument("--smp", dest="smp", action="store_true", default=None, help="start the secondaries (default: L5 only)")
    ap.add_argument("--no-smp", dest="smp", action="store_false")
    ap.add_argument("--args", default="", help="extra boot arguments, appended")
    ap.add_argument("--fdt-addr", type=lambda s: int(s, 0), help="address of the prepared FDT if the console line is not captured")
    ap.add_argument("--fdt-dump", help="path of the fdt_m1_dump binary (default: built from the bundle's sources)")
    ap.add_argument("--boot-timeout", type=float, default=40.0, help="seconds to wait for kboot_boot's reply (default 40, as linux.py)")
    ap.add_argument("--dry-run", action="store_true", help="no device: print every proxy call (the 'prepared' FDT is the DTB itself)")
    ap.add_argument("--go", action="store_true", help="required for rungs L1-L5 on a real device (they jump)")
    a = ap.parse_args(argv)
    a.bundle = os.path.abspath(a.bundle)
    a.out = os.path.abspath(a.out or os.path.join(a.bundle, "out"))
    return a


def main(argv=None, target=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    try:
        if target is None:
            if args.dry_run:
                say("DRY RUN: no device is opened; every proxy call is printed.")
                target = DryTarget()
            else:
                if args.rung != "L0" and not args.go:
                    raise LoadError("rung %s jumps into the layer: add --go once you mean it (try --dry-run first)" % args.rung, 2)
                if not args.m1n1:
                    raise LoadError("no m1n1 proxyclient: pass --m1n1 <checkout> or set M1N1_DIR", 2)
                target = connect(args.m1n1, args.device)
        return run(args, target)
    except LoadError as e:
        say("ERROR: %s" % e)
        return e.code


if __name__ == "__main__":
    sys.exit(main())
