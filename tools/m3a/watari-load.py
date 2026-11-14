#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""watari-load: host loader of the M3a hardware day (DESIGN 2.5, PLAN S17).

Runs on the HOST Mac (Ciani's), with the target Mac in m1n1 proxy mode. It uses the m1n1 proxyclient (GPL-2.0, NOT
vendored: the checkout of day 1, path from --m1n1 or env M1N1_DIR). It imports only m1n1.proxy, m1n1.proxyutils and
m1n1.sysreg. It NEVER imports m1n1.setup: setup.py resets a panic counter in the PMU (an SPMI write, F-29).

Call order of a run (the unit test test_watari_load.py asserts it):
  1. verify the bundle against SHA256SUMS (refuse otherwise)
  2. allocate + write: DTB (malloc), layer (memalign 32 MB), guest (memalign 32 MB, rungs L3-L5), EDK2 (memalign 32 MB,
     its own block, rungs D0/D2: --edk2 <file>; wel2.guest = its entry, wel2.abi=fdt, wel2.gfdt=build, wel2.fb=<--fb>)
  3. cache maintenance: dc cvau + ic ivau over each image
  4. ONLY NOW set the boot arguments (they contain the guest address, DESIGN L-2): kboot_set_chosen("bootargs", ...)
  5. optional smp_start_secondaries (rung L5)
  6. kboot_prepare_dt(dtb)   (m1n1 fills the DTB from the ADT; a non-zero return aborts before anything runs)
  7. rung L0 and D0 stop here: read the prepared FDT back, save it in the host folder, run fdt_m1_dump on it (D0 also
     gfdt_dump: the guest FDT the layer would build, and the placement check of the EDK2 block). NO jump.
     D2 does the same read-back and the same checks, then jumps; a read-back that cannot be done (exit 3) or any refusal stops it.
  8. DAIF = 0xc0, kboot_boot(layer). USB goes away during this request: a lost reply or a timeout AFTER the request was
     sent counts as "handed over".

The loader writes only RAM (heap blocks, the DTB, m1n1's FDT copy). m1n1's own kboot_prepare_dt / kboot_boot do
non-persistent hardware initialisation (ISP heap, display DART lock, USB/PCIe bring-up, SEP random bytes), all reset by a
reboot [hypothesis: not every helper was read]. Nothing is written to storage, NVRAM or the PMU by us.

Rung W1 (DESIGN watari-winram 2, PLAN T23): `--ramdisk <image>` (D0/D2 with --edk2) adds a FOURTH block, the Windows RAM-disk image:
the image is checked against its `.sha256` sidecar BEFORE any proxy call; its block (image + a 4 KiB trailer at the END of the
range, rounded to 1 MiB, 32 MB aligned) is allocated FIRST (before the layer block), written in 16 MiB slices (progress, bounded
retry, read-back), the trailer last, caches cleaned over the whole range, and `wel2.ramdisk=0x<base>,0x<size>` goes into the
boot arguments. `--ramdisk-test <MiB>` is the proxy rate test: one block, write + read back, MB/s, no DTB, no layer, no jump.

Exit codes: 0 ok / handed over, 1 error, 2 usage or bundle problem, 3 L0/D0: prepared but cannot read the FDT back,
4 L0/D0/D2: a heap block is not wholly inside usable RAM, or the layer's placement rule would refuse the EDK2 block.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

BLOCK = 32 * 1024 * 1024          # DESIGN D-2: one stage-2 block of the 16 KB granule
MIB = 1024 * 1024
RD_TRAILER = 4096                 # watari-winram DESIGN 2.2: 4 KiB at the END of the ramdisk range, written last
RD_MAGIC = b"WRAMDSK1"
RD_ALIGN = 16384                  # the layer refuses a ramdisk base or size that is not 16 KB aligned (DESIGN 3.1)
RD_MAX = 896 * MIB                # DESIGN 2.3: heap budget of ProxyUtils (1 GiB) minus 32 MiB skip and three 32 MiB blocks
RD_SLICE_MIB = 16
HEAP_BYTES = 1024 * MIB           # ProxyUtils' default heap [verified: DESIGN watari-winram 2.3, m1n1 proxyutils.py]

# rung -> (layer image, guest image or None, step or None, start secondaries)
RUNGS = {
    "L0": (None, None, None, True),
    "L1": ("wel2-m1.bin", None, 1, False),
    "L2": ("wel2-m1.bin", None, 2, False),
    "L3": ("wel2-chain.bin", "m1guest.bin", 3, False),
    "L4": ("wel2-chain.bin", "m1guest.bin", 4, False),
    "L5": ("wel2-chain.bin", "m1guest.bin", 5, True),
    # Day 3 (PLAN T13 H2-H7): the guest is EDK2 (--edk2 <file>), the test guest is not loaded. D0 prepares and previews, no
    # jump; D2 jumps (chain layer, no wel2.step). D1 is "--rung L5 --fb band" (check G12) [check]; D3-D5 are D2 with other options.
    "D0": (None, None, None, True),
    "D2": ("wel2-chain.bin", None, None, False),
}
EDK2_RUNGS = ("D0", "D2")
FB_MODES = ("layer", "band", "guest")


class LoadError(Exception):
    def __init__(self, msg, code=1):
        super().__init__(msg)
        self.code = code


class ChecksumError(LoadError):
    """A write whose m1n1 data checksum did not match (the fake targets raise it). The real target maps onto it what m1n1
    v1.6.1 proxy.py raises for a bad write [verified: AsahiLinux/m1n1 v1.6.1 proxyclient/m1n1/proxy.py]: the reply status
    ST_CSUMERR becomes UartRemoteError("Reply error: Data checksum failed"), a corrupted reply becomes UartChecksumError.
    A LoadError subclass, so one that escapes the slice retry (the trailer write) is a clean exit 1, not a traceback."""


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

def build_bootargs(step, cpuif, guest_addr, extra, edk2=False, fb="layer", ramdisk=None):
    """wel2.* arguments (el2/boot/README.md). Built only after the guest block exists (its address is in them).
    edk2: the guest is EDK2: wel2.abi=fdt wel2.gfdt=build (the layer builds the guest FDT) and always wel2.fb=<mode>.
    The mode is passed through as given; nothing here depends on which modes the layer knows. Without edk2, wel2.fb is
    added only when it is not the default "layer"."""
    parts = []
    if step is not None:
        parts.append("wel2.step=%d" % step)
    if cpuif and ((step is not None and step >= 2) or edk2):
        parts.append("wel2.cpuif=%s" % cpuif)
    if guest_addr is not None:
        parts.append("wel2.guest=0x%x" % guest_addr)
    if ramdisk is not None:
        parts.append("wel2.ramdisk=0x%x,0x%x" % ramdisk)         # (base, size); size includes the 4 KiB trailer
    if edk2:
        parts.append("wel2.abi=fdt")
        parts.append("wel2.gfdt=build")
    if edk2 or (fb and fb != "layer"):
        parts.append("wel2.fb=%s" % fb)
    if extra:
        parts.extend(extra.split())
    return " ".join(parts)


# ----------------------------------------------------------------------------------------------------------------------
# Targets: the only objects that talk to a device (real) or print and record (dry run / unit test)
# ----------------------------------------------------------------------------------------------------------------------

class DryTarget:
    """No device. Prints every proxy call and records it in self.calls. Also the base of the unit test's fake."""
    transport_errors = (OSError,)
    checksum_errors = (ChecksumError,)
    store_limit = None                      # writes larger than this are not kept (a real --dry-run of a 700 MB image)
    dry_run = True                          # a real --dry-run (fake addresses, the "prepared" FDT is the DTB); the unit test's fake says False

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
        if self.store_limit is None or len(data) <= self.store_limit:
            self.mem[addr] = bytes(data)
        self._log("writemem", addr, len(data))

    def read(self, addr, n):
        self._log("readmem", addr, n)
        for base, d in self.mem.items():
            if base <= addr < base + len(d):
                return d[addr - base:addr - base + n]
        return b"\0" * n

    def disable_data_csums(self):
        self._log("disable_data_csums")

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
    dry_run = False

    checksum_errors = (ChecksumError,)

    def __init__(self, iface, p, u, errors, daif_reg, csum_errors=(), feature_flag=None):
        self.iface, self.p, self.u = iface, p, u
        self.transport_errors = errors
        self._csum_src = csum_errors          # what proxy.py raises for a checksum problem (see ChecksumError)
        self._feature = feature_flag          # m1n1.proxy.Feature.DISABLE_DATA_CSUMS, or None if this client lacks it
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

    def _is_csum(self, e):
        # UartRemoteError is also "Bad Command" / "Invalid argument" / "Data transfer failed": only its checksum text counts
        return type(e).__name__ != "UartRemoteError" or "checksum" in str(e).lower()

    def write(self, addr, data):
        try:
            self.iface.writemem(addr, data, len(data) <= MIB)      # the dot-per-8-KiB progress only for small writes
        except self._csum_src as e:
            if not self._is_csum(e):
                raise
            raise ChecksumError("m1n1 reported a data checksum failure: %s" % e)

    def read(self, addr, n):
        try:
            return self.iface.readmem(addr, n)
        except self._csum_src as e:
            raise LoadError("read-back of %d bytes at 0x%x: %s: %s" % (n, addr, type(e).__name__, e))

    def disable_data_csums(self):
        """m1n1 v1.6.1 proxy.py has no API to switch this on: Feature.DISABLE_DATA_CSUMS is offered to the target by every
        UartInterface.nop() (Feature.get_all()) and is on iff the firmware agreed; the state is `iface.enabled_features`
        [verified: v1.6.1 proxy.py]. So --no-data-csums cannot ask for anything: it only requires that the feature is already on
        and refuses (exit 2, before any write) otherwise."""
        on = bool(self._feature is not None and (getattr(self.iface, "enabled_features", 0) & self._feature))
        if not on:
            raise LoadError("--no-data-csums: the target did not enable Feature.DISABLE_DATA_CSUMS in the proxy handshake (an older "
                            "m1n1 or client); it cannot be switched on from here. Run without the flag", 2)

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
    csum_src, feature = (), None
    try:
        from m1n1.proxy import UartChecksumError, UartRemoteError
        csum_src = (UartChecksumError, UartRemoteError)
    except ImportError:
        pass
    try:
        from m1n1.proxy import Feature
        feature = Feature.DISABLE_DATA_CSUMS
    except (ImportError, AttributeError):
        pass
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
    return M1n1Target(iface, p, u, errors, DAIF, csum_src, feature)


# ----------------------------------------------------------------------------------------------------------------------
# The Windows RAM-disk image (fourth block): sidecar check, trailer, sliced transfer, rate test
# ----------------------------------------------------------------------------------------------------------------------

def round_up(n, a):
    return (n + a - 1) // a * a


def ramdisk_range_size(image_bytes):
    """The block the layer is told about: the image, then the 4 KiB trailer at the END of the range (DESIGN 2.2, EDK2 reads it
    at base + size - 4096 and requires image_bytes <= size - 4096), rounded up to 1 MiB (so 16 KB aligned)."""
    return round_up(image_bytes + RD_TRAILER, MIB)


def heap_need(range_size):
    """Worst-case use of ProxyUtils' 1 GiB heap: the 32 MiB alignment skip, the image block rounded to 32 MiB, three more 32 MiB
    blocks (layer, spacer, EDK2). The small DTB malloc sits below the skip."""
    return BLOCK + round_up(range_size, BLOCK) + 3 * BLOCK


def check_ramdisk_size(image_bytes):
    if image_bytes <= 0:
        raise LoadError("the ramdisk image is empty", 2)
    rs = ramdisk_range_size(image_bytes)
    if rs > RD_MAX or heap_need(rs) > HEAP_BYTES:
        raise LoadError("the ramdisk image (%d bytes, %.1f MiB with its trailer) is larger than the %d MiB limit of m1n1's 1 GiB "
                        "proxy heap (DESIGN watari-winram 2.3); a larger image needs ProxyUtils(heap_size=...)" %
                        (image_bytes, rs / MIB, RD_MAX // MIB), 2)
    return rs


class RamdiskImage:
    """The image file, hashed once (SHA-256 for the sidecar, CRC-32 for the trailer) without holding it in memory."""

    def __init__(self, path, size, sha, crc):
        self.path, self.size, self.sha256, self.crc32 = path, size, sha, crc
        self.range_size = ramdisk_range_size(size)
        self._h, self._next = hashlib.sha256(), 0

    def read(self, offset, n):
        with open(self.path, "rb") as f:
            f.seek(offset)
            d = f.read(n)
        if offset == self._next:                      # sequential reads: hash what is actually sent (the file may change after
            self._h.update(d)                         # the sidecar check; see check_streamed)
            self._next += len(d)
        return d

    def check_streamed(self):
        """After the transfer: the bytes read for sending hash to the sidecar value (else the file changed under us)."""
        if self._next != self.size or self._h.hexdigest() != self.sha256:
            raise LoadError("--ramdisk %s changed on disk after the sidecar check (the bytes sent differ): aborting before the "
                            "trailer; nothing was handed over" % self.path)


def inspect_ramdisk(path):
    """Hash the file in 4 MiB chunks, compare the SHA-256 with the sidecar `<path>.sha256` (first 64-hex token of its first
    line, the format `shasum -a 256` writes), check the size limit. Raises LoadError (exit 2) BEFORE any proxy call."""
    if not os.path.isfile(path):
        raise LoadError("--ramdisk %s: no such file" % path, 2)
    side = path + ".sha256"
    if not os.path.isfile(side):
        raise LoadError("--ramdisk %s: no sidecar %s (the image tool writes it): refusing an unverified image" %
                        (path, os.path.basename(side)), 2)
    with open(side) as f:
        m = re.match(r"^\s*([0-9a-fA-F]{64})\b", f.readline())
    if not m:
        raise LoadError("%s: first line is not a SHA-256" % side, 2)
    want = m.group(1).lower()
    size = os.path.getsize(path)
    check_ramdisk_size(size)
    h, crc = hashlib.sha256(), 0
    with open(path, "rb") as f:
        while True:
            chunk = f.read(4 * MIB)
            if not chunk:
                break
            h.update(chunk)
            crc = zlib.crc32(chunk, crc)
    if h.hexdigest() != want:
        raise LoadError("--ramdisk %s: SHA-256 differs from its sidecar (image damaged, edited or half copied): refusing to "
                        "run\n  image   %s\n  sidecar %s" % (path, h.hexdigest(), want), 2)
    return RamdiskImage(path, size, h.hexdigest(), crc & 0xffffffff)


def load_ramdisk(args):
    """Resolve --ramdisk (a path, or a name inside the bundle) and run the sidecar check. Exit 2 on any problem."""
    path = args.ramdisk
    if not os.path.isfile(path) and os.path.isfile(os.path.join(args.bundle, path)):
        path = os.path.join(args.bundle, path)
    say("hashing the ramdisk image (%s) ..." % path)
    return inspect_ramdisk(path)


def prevalidate(args):
    """Everything about the ramdisk options that needs no device, done BEFORE the proxy is opened: opening it already sends
    requests (bootstrap_port: NOP, set_baud; ProxyUtils: reads, heapblock_set_limit), so the SHA-256 check must come first."""
    if not 1 <= args.ramdisk_slice <= 64:
        raise LoadError("--ramdisk-slice must be 1..64 MiB", 2)
    if args.ramdisk_test is not None:
        if not 1 <= args.ramdisk_test <= RD_MAX // MIB - 4:
            raise LoadError("--ramdisk-test %d: between 1 and %d MiB" % (args.ramdisk_test, RD_MAX // MIB - 4), 2)
        return
    if args.ramdisk:
        if not (args.edk2 and args.rung in EDK2_RUNGS):
            raise LoadError("--ramdisk goes with --edk2 on rung D0 (preview) or D2 (jump): the layer needs wel2.abi=fdt for it", 2)
        args._rd = load_ramdisk(args)


def build_trailer(image, crc=None, sha=None):
    """The 4 KiB trailer (DESIGN 2.2): magic `WRAMDSK1`, image_bytes u64, CRC-32 u32, SHA-256 (32 bytes, informational), zero
    padding; little endian. `image` is a RamdiskImage or the image bytes themselves. The CRC is `zlib.crc32` (the IEEE 802.3
    polynomial) **[hypothesis: it is the polynomial of UEFI CalculateCrc32; settled by T21b's test against the firmware]**."""
    if isinstance(image, RamdiskImage):
        n, crc, sha = image.size, image.crc32, bytes.fromhex(image.sha256)
    else:
        n, crc, sha = len(image), zlib.crc32(image) & 0xffffffff, hashlib.sha256(image).digest()
    t = RD_MAGIC + struct.pack("<QI", n, crc) + sha
    return t + b"\0" * (RD_TRAILER - len(t))


def fmt_eta(sec):
    sec = int(round(sec))
    return "%d min %02d s" % (sec // 60, sec % 60) if sec >= 60 else "%d s" % sec


def write_sliced(target, addr, reader, total, slice_bytes, label="ramdisk", verify="sample", clock=time.monotonic, out=None):
    """Write `total` bytes (reader(offset, n) -> bytes) to `addr` in `slice_bytes` slices, each its own writemem (its own m1n1
    checksum). A checksum error retries THAT slice once, then aborts (DESIGN 2.2). After each slice: verify "sample" (first and
    last 4 KiB read back), "full" (the whole slice) or "none". One progress line per slice: bytes, MB/s (decimal), ETA.
    Returns the elapsed seconds."""
    out = out or say
    nsl = (total + slice_bytes - 1) // slice_bytes
    t0 = clock()
    compared = not target.dry_run
    if verify != "none" and not compared:
        out("  (dry run: nothing is stored on the target, the read-back is not compared)")
    for i in range(nsl):
        off = i * slice_bytes
        n = min(slice_bytes, total - off)
        data = reader(off, n)
        if len(data) != n:
            raise LoadError("%s: short read at offset %d (%d of %d bytes)" % (label, off, len(data), n))
        for attempt in (1, 2):
            try:
                target.write(addr + off, data)
                break
            except target.checksum_errors as e:
                if attempt == 2:
                    raise LoadError("%s slice %d/%d: checksum error twice (%s): aborting; nothing was handed over" %
                                    (label, i + 1, nsl, e))
                out("  %s slice %d/%d: checksum error, retrying this slice once" % (label, i + 1, nsl))
        if verify != "none" and compared:
            spans = [(0, n)] if verify == "full" else [(0, min(4096, n)), (max(0, n - 4096), min(4096, n))]
            for so, sn in spans:
                back = target.read(addr + off + so, sn)
                if back != data[so:so + sn]:
                    raise LoadError("%s slice %d/%d: read-back differs at offset 0x%x (+%d): aborting" %
                                    (label, i + 1, nsl, off + so, sn))
        el = clock() - t0
        done = off + n
        rate = done / el / 1e6 if el > 0 else 0.0
        eta = (total - done) / (done / el) if el > 0 else 0.0
        out("  %s slice %d/%d: %7.1f / %.1f MiB  %6.2f MB/s  ETA %s" %
            (label, i + 1, nsl, done / MIB, total / MIB, rate, fmt_eta(eta)))
    return clock() - t0


def test_pattern(i, n):
    """Deterministic, slice-specific bytes for the rate test (no os.urandom: the read-back must compare)."""
    return (hashlib.sha256(b"watari-rate-test-%d" % i).digest() * (n // 32 + 1))[:n]


def run_ramdisk_test(args, target):
    """--ramdisk-test <MiB>: allocate one block, write MiB of test pattern in slices, read it back and compare, print MB/s each
    way. No DTB, no layer, no boot arguments, no jump, no write outside the block. The first step of rung W1."""
    mib = args.ramdisk_test
    if not 1 <= mib <= RD_MAX // MIB - 4:
        raise LoadError("--ramdisk-test %d: between 1 and %d MiB" % (mib, RD_MAX // MIB - 4), 2)
    verified = verify_bundle(args.bundle)
    say("bundle OK: %d files match SHA256SUMS (%s)" % (len(verified), args.bundle))
    total, sl = mib * MIB, args.ramdisk_slice * MIB
    say("rate test: %d MiB in %d MiB slices (rung W1, first step); no DTB, no layer, no jump; RAM of the target only" %
        (mib, args.ramdisk_slice))
    target.set_timeout(max(args.boot_timeout, 40))
    if args.no_data_csums:
        target.disable_data_csums()
        say("data checksums disabled (--no-data-csums): a corrupted slice is only caught by the read-back")
    base = target.memalign(BLOCK, round_up(total, MIB))
    say("BLOCK  rate-test block 0x%x..0x%x" % (base, base + round_up(total, MIB)))
    t_w = write_sliced(target, base, lambda off, n: test_pattern(off // sl, n), total, sl, "write", "none")
    t_r0 = time.monotonic()
    bad = None
    for i in range((total + sl - 1) // sl):
        off, n = i * sl, min(sl, total - i * sl)
        back = target.read(base + off, n)
        if not target.dry_run and back != test_pattern(i, n):
            bad = off
            break
    t_r = time.monotonic() - t_r0
    wr = total / t_w / 1e6 if t_w > 0 else 0.0
    rr = total / t_r / 1e6 if t_r > 0 else 0.0
    if bad is not None:
        say("RATE TEST: FAIL - read-back differs in the slice at offset 0x%x" % bad)
        return 1
    say("RATE TEST: write %.2f MB/s, read %.2f MB/s, read-back %s" %
        (wr, rr, "not compared (dry run)" if target.dry_run else "identical"))
    if wr > 0:
        say("  a 704 MiB image at the write rate: about %s [hypothesis: the rate holds for a long transfer]" %
            fmt_eta(704 * MIB / (wr * 1e6)))
        say("  DESIGN 2.3: below 0.5 MB/s the plan stops (24 min); 1 MB/s = 12 min is acceptable once; 3 MB/s and up is comfortable")
    return 0


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


def parse_fb_range(dump_text):
    """('framebuffer base=0x.. WxH stride=S ...' of fdt_m1_dump) -> (base, end) or None."""
    m = re.search(r"^framebuffer base=0x([0-9a-f]+) (\d+)x(\d+) stride=(\d+)", dump_text, re.M)
    if not m:
        return None
    base = int(m.group(1), 16)
    return base, base + int(m.group(3)) * int(m.group(4))


def check_blocks(dump_text, blocks, avoid=()):
    """Is each block wholly inside ONE usable range (FDT RAM minus reservations)? A block is an address (a 32 MB aligned 32 MB
    block, the layer rounds the usable ranges inward to 32 MB, which keeps any such block that fits) or (addr, size, align)
    for the ramdisk (checked over its WHOLE range). No block may overlap another block or an `avoid` range (name, start, end:
    the prepared FDT, the framebuffer). Returns (all_ok, report lines)."""
    usable, truncated = parse_usable(dump_text)
    spans = {}
    for name, b in blocks.items():
        spans[name] = (b, BLOCK, BLOCK) if isinstance(b, int) else tuple(b)
    lines, ok_all = [], True
    for name, (addr, size, align) in spans.items():
        end = addr + size
        hit = [r for r in usable if r[0] <= addr and end <= r[0] + r[1]]
        why = []
        if not hit:
            why.append("not wholly inside one usable range")
        if addr % align:
            why.append("not %d KB aligned" % (align // 1024))
        for other, (oa, osz, _) in spans.items():
            if other != name and addr < oa + osz and oa < end:
                why.append("overlaps the %s block" % other)
        for aname, a0, a1 in avoid:
            if addr < a1 and a0 < end:
                why.append("overlaps the %s (0x%x..0x%x)" % (aname, a0, a1))
        ok = not why
        ok_all &= ok
        if ok:
            res = "PASS (inside usable range 0x%x..0x%x)" % (hit[0][0], hit[0][0] + hit[0][1])
        else:
            res = "FAIL (%s)" % "; ".join(why)
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


def ensure_dump_tool(bundle, given, name="fdt_m1_dump"):
    """Prefer the prebuilt arm64 tool of the bundle (covered by SHA256SUMS). Never run `cc` blind: on a Mac
    without the Command Line Tools /usr/bin/cc can open the installer (an install on a zero-footprint host)."""
    for tool in (given, os.path.join(bundle, name)):
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
        say("building %s with the installed Command Line Tools (output stays in the bundle folder) ..." % name)
        subprocess.run(["sh", build], check=True)
        built = os.path.join(bundle, "fdt-dump-src", name)
        if os.access(built, os.X_OK):
            return built
    raise LoadError("no usable %s (prebuilt one missing or not runnable, Command Line Tools not installed): "
                    "the prepared FDT is saved; check it on Marco's Mac with the same tool" % name)


def gran_from_args(extra):
    """The granule the layer will pick from --args: stage1.c tests wel2.gran=16k first, then wel2.gran=4k (whole tokens).
    Returns "16k", "4k" or None (the layer default, which gfdt_dump also uses)."""
    toks = (extra or "").split()
    if "wel2.gran=16k" in toks:
        return "16k"
    if "wel2.gran=4k" in toks:
        return "4k"
    return None


def tool_has_option(tool, opt):
    """Does the tool know `opt`? Looks for the option string in the binary (or script) without running it: gfdt_dump
    gets --ramdisk from T21a; until then the loader says so and skips that part of the check."""
    try:
        with open(tool, "rb") as f:
            return opt.encode() in f.read(8 * MIB)
    except OSError:
        return False


def run_gfdt(tool, blob_path, layer_addr, entry, image_size, fb, gran=None, ramdisk=None):
    """gfdt_dump on the prepared FDT: the guest FDT the layer would build. Returns (rc, stdout). Prints the tool's stdout;
    its stderr lines (NOTE / PRIVACY / refusal reasons) are echoed. Output is the guest tree, never the prepared FDT."""
    out = os.path.join(os.path.dirname(blob_path), "guest-fdt.dtb")
    if os.path.exists(out):
        os.remove(out)
    cmd = [tool, blob_path, "--layer", "0x%x" % layer_addr, "--entry", "0x%x" % entry,
           "--image-size", "0x%x" % image_size, "--fb", fb]
    if gran:
        cmd += ["--gran", gran]
    if ramdisk is not None:
        cmd += ["--ramdisk", "0x%x,0x%x" % ramdisk]
    cmd += ["-o", out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    say("== gfdt_dump (the guest FDT the layer would build; no serial number, MAC or seed can appear) ==")
    sys.stdout.write(r.stdout)
    for ln in r.stderr.splitlines():
        say("  gfdt_dump: " + ln)
    if r.returncode == 0 and os.path.isfile(out) and shutil.which("dtc"):
        d = subprocess.run(["dtc", "-q", "-I", "dtb", "-O", "dts", out], capture_output=True, text=True)
        if d.returncode == 0:
            say("== the same guest FDT through dtc ==")
            sys.stdout.write(d.stdout)
    return r.returncode, r.stdout


def run(args, target):
    """The whole sequence on `target`. Returns the process exit code."""
    layer_name, guest_name, step, smp = RUNGS[args.rung]
    bundle = args.bundle
    if args.smp is not None:
        smp = args.smp

    edk2_name = None
    if args.edk2:
        # the EDK2 image must be a file of the bundle (SHA256SUMS covers it): a bare name, or a path inside the bundle
        full = os.path.abspath(os.path.join(bundle, args.edk2))
        if os.path.dirname(full) != bundle:
            raise LoadError("--edk2 %s is not a file of the bundle %s (it must be listed in SHA256SUMS)" % (args.edk2, bundle), 2)
        edk2_name = os.path.basename(full)
    if args.rung in EDK2_RUNGS and not edk2_name:
        raise LoadError("rung %s needs --edk2 <file of the bundle> (WATARI_M1_EFI.fd)" % args.rung, 2)
    if edk2_name and args.rung not in EDK2_RUNGS + ("L0",):
        raise LoadError("--edk2 goes with rung D0 or D2 (or L0: the same preview as D0)", 2)
    preview = args.rung in ("L0", "D0")                # prepares the FDT, reads it back, never jumps
    if args.ramdisk and not (edk2_name and args.rung in EDK2_RUNGS):
        raise LoadError("--ramdisk goes with --edk2 on rung D0 (preview) or D2 (jump): the layer needs wel2.abi=fdt for it", 2)
    if not 1 <= args.ramdisk_slice <= 64:
        raise LoadError("--ramdisk-slice must be 1..64 MiB", 2)
    required = [x for x in (layer_name, guest_name, edk2_name) if x]
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
    rd = None
    if args.ramdisk:
        # the image and its sidecar are checked here, BEFORE any proxy call (nothing below this block has touched the target)
        rd = getattr(args, "_rd", None)                   # main() already did it before opening the device
        if rd is None:
            rd = load_ramdisk(args)
        path = rd.path
        sl = args.ramdisk_slice * MIB
        say("RAMDISK PLAN: %s: %d bytes (%.1f MiB), sha256 %s (matches its sidecar), crc32 0x%08x" %
            (os.path.basename(path), rd.size, rd.size / MIB, rd.sha256[:16], rd.crc32))
        say("  block %d bytes (%.1f MiB: image + 4 KiB trailer at the end, rounded to 1 MiB), 32 MB aligned, allocated first" %
            (rd.range_size, rd.range_size / MIB))
        say("  %d slices of %d MiB (%s read-back each); heap need <= %.0f MiB of the proxy heap's %d MiB" %
            ((rd.size + sl - 1) // sl, args.ramdisk_slice, args.ramdisk_verify, heap_need(rd.range_size) / MIB, HEAP_BYTES // MIB))
        say("  transfer time at 1 / 3 / 10 MB/s: %s / %s / %s [hypothesis: the rate is unmeasured, run --ramdisk-test first]" %
            tuple(fmt_eta(rd.size / (r * 1e6)) for r in (1, 3, 10)))
    layer = guest = None
    if layer_name:
        layer = load(layer_name)
        check_image(layer_name, layer)
    if guest_name:
        guest = load(guest_name)
        check_image(guest_name, guest, guest=True)
    edk2 = None
    if edk2_name:
        edk2 = load(edk2_name)
        check_image(edk2_name, edk2)

    say("rung %s: layer=%s guest=%s edk2=%s ramdisk=%s step=%s cpuif=%s fb=%s secondaries=%s" %
        (args.rung, layer_name or "-", guest_name or "-", edk2_name or "-", os.path.basename(rd.path) if rd else "-",
         step if step is not None else "-", args.cpuif if ((step or 0) >= 2 or edk2_name) else "-", args.fb, smp))
    if not preview:
        say("rung %s ends with a JUMP to the layer; the target leaves m1n1 (recovery: power button ~10 s)." % args.rung)

    # --- 2. allocate and write ---------------------------------------------------------------------------------------
    dtb_addr = target.malloc(len(dtb))
    target.write(dtb_addr, dtb)
    say("DTB    %-22s %7d bytes at 0x%x  sha256 %s" % (dtb_name, len(dtb), dtb_addr, sha256(dtb)[:16]))
    layer_addr = guest_addr = edk2_addr = rd_addr = None
    l0_blocks = {}
    if rd is not None:
        # FIRST block: the heap is first-fit from the low end, so the range above the image stays one large /memory piece for EDK2
        if args.no_data_csums:
            target.disable_data_csums()
            say("data checksums disabled (--no-data-csums)")
        target.set_timeout(max(args.boot_timeout, 40))    # the proxy default is 3 s: too short for a 16 MiB slice at ~1 MB/s
        rd_addr = target.memalign(BLOCK, rd.range_size)
        if rd_addr % BLOCK:
            raise LoadError("the ramdisk block is not 32 MB aligned (0x%x)" % rd_addr)
        say("RAMDISK block 0x%x..0x%x (%d bytes)" % (rd_addr, rd_addr + rd.range_size, rd.range_size))
        el = write_sliced(target, rd_addr, rd.read, rd.size, args.ramdisk_slice * MIB, "ramdisk", args.ramdisk_verify)
        rd.check_streamed()
        trailer = build_trailer(rd)
        taddr = rd_addr + rd.range_size - RD_TRAILER
        target.write(taddr, trailer)                      # last
        if not target.dry_run and target.read(taddr, RD_TRAILER) != trailer:
            raise LoadError("ramdisk trailer: read-back differs")
        say("RAMDISK written: %d bytes in %s (%.2f MB/s), trailer at 0x%x (crc32 0x%08x)" %
            (rd.size, fmt_eta(el), rd.size / el / 1e6 if el > 0 else 0.0, taddr, rd.crc32))
    if preview:
        # L0 allocates the blocks the jump rungs would use, WITHOUT writing them: the host then checks that m1n1's proxy
        # heap is plain usable RAM (the main unverified risk for L3). With --edk2 (D0) a third block is allocated, in the
        # same order D2 allocates (layer, guest = a spacer D2 leaves unwritten, EDK2), so D2 gets the addresses D0 checked.
        l0_blocks = {"layer": target.memalign(BLOCK, BLOCK), "guest": target.memalign(BLOCK, BLOCK)}
        if edk2_name:
            l0_blocks["edk2"] = target.memalign(BLOCK, BLOCK)
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
    if edk2 is not None and not preview:
        spacer = target.memalign(BLOCK, BLOCK)          # the "guest" block of D0's check: kept so the addresses are D0's
        say("SPACER 32 MB aligned block at 0x%x (not written: keeps D2's addresses equal to D0's)" % spacer)
        edk2_addr = target.memalign(BLOCK, BLOCK)
        if edk2_addr % BLOCK:
            raise LoadError("the EDK2 block is not 32 MB aligned (0x%x)" % edk2_addr)
        target.write(edk2_addr, edk2)
        say("EDK2   %-22s %7d bytes at 0x%x  sha256 %s" % (edk2_name, len(edk2), edk2_addr, sha256(edk2)[:16]))

    # --- 3. cache maintenance ----------------------------------------------------------------------------------------
    for addr, length in ((rd_addr, rd.range_size if rd else 0), (layer_addr, len(layer) if layer else 0),
                         (guest_addr, len(guest) if guest else 0), (edk2_addr, len(edk2) if edk2 and not preview else 0)):
        for off in range(0, length, 32 * MIB):    # chunks: one proxy request per 32 MiB stays far below the reply timeout
            n = min(32 * MIB, length - off)
            target.dc_cvau(addr + off, n)
            target.dc_cvac(addr + off, n)     # to the point of coherency; the real guarantee is m1n1's set/way flush in mmu_shutdown
            target.ic_ivau(addr + off, n)

    # --- 4. boot arguments, after the allocations --------------------------------------------------------------------
    bootargs = build_bootargs(step, args.cpuif, edk2_addr if edk2_addr is not None else guest_addr, args.args,
                              edk2=edk2_addr is not None, fb="layer" if preview else args.fb,
                              ramdisk=(rd_addr, rd.range_size) if rd is not None and not preview else None)
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

    # --- 7. rungs L0 / D0: read it back, check it, stop. D2: the same, then the jump ----------------------------------
    if preview or edk2_name:
        label = args.rung
        path = None
        if fdt_addr is None:
            say("%s: the prepared FDT address could not be captured from m1n1's console text." % label)
            say("Read it from the m1n1 console (line 'FDT prepared at 0x...') and rerun with --fdt-addr 0x...")
        else:
            hdr = target.read(fdt_addr, 40)
            total = struct.unpack_from(">I", hdr, 4)[0] if len(hdr) >= 8 else 0
            if hdr[:4] != b"\xd0\x0d\xfe\xed":
                say("%s: no FDT magic at 0x%x (wrong address?)" % (label, fdt_addr))
            elif not 40 <= total <= 4 * 1024 * 1024:
                say("%s: implausible totalsize %d in the FDT header" % (label, total))
            else:
                blob = target.read(fdt_addr, total)
                os.makedirs(args.out, exist_ok=True)
                path = os.path.join(args.out, "prepared-fdt.dtb")
                with open(path, "wb") as f:
                    f.write(blob)
                say("prepared FDT: %d bytes at 0x%x, saved to %s (host folder only: it holds the board serial number;" %
                    (total, fdt_addr, path))
                say("never commit it, never paste it). sha256 %s" % sha256(blob)[:16])
        if path is None:
            # the checks could not be made: no jump, on any rung (D2 included). Safe default, no override flag.
            say("%s: no read-back, so the block and placement checks cannot be made: NOT jumping. Rerun with "
                "--fdt-addr 0x<address from the m1n1 console line 'FDT prepared at ...'>." % label)
            return 3
        else:
            if isinstance(target, DryTarget):
                say("(dry run: the 'prepared' FDT is the unmodified DTB; on hardware m1n1 adds memory, cpus, framebuffer)")
            try:
                dump_text = run_dump(ensure_dump_tool(bundle, args.fdt_dump), path)
            except OSError as e:
                say("fdt_m1_dump could not be run (%s: %s): NOT jumping." % (type(e).__name__, e))
                return 1
            blocks = {}
            if rd is not None:
                blocks["ramdisk"] = (rd_addr, rd.range_size, RD_ALIGN)       # the WHOLE range, not its first 32 MB
            if preview:
                blocks.update(l0_blocks)
            else:
                blocks.update({"layer": layer_addr, "edk2": edk2_addr})      # the spacer is never used
            if rd is None:
                say("== block placement check (host side): are the layer/%s blocks wholly inside usable RAM? ==" %
                    ("guest/edk2" if preview and edk2_name else "guest" if preview else "edk2"))
            else:
                say("== block placement check (host side): are the %s blocks wholly inside usable RAM, apart from each other, "
                    "the prepared FDT and the framebuffer? ==" % "/".join(blocks))
            avoid = [("prepared FDT", fdt_addr, fdt_addr + total)] if rd is not None else []
            fbr = parse_fb_range(dump_text) if rd is not None else None
            if fbr:
                avoid.append(("framebuffer", fbr[0], fbr[1]))
            ok, lines = check_blocks(dump_text, blocks, avoid)
            for ln in lines:
                say(ln)
            if isinstance(target, DryTarget):
                say("(dry run: the fake addresses are not m1n1's heap; this only exercises the check)")
            if not ok:
                say("BLOCK CHECK: FAIL - the heap blocks are not plain usable RAM in this FDT. Do NOT run L3-L5 / D2: the "
                    "layer would refuse the guest. L1/L2 do not need the guest block but still use the layer block.")
                return 4
            say("BLOCK CHECK: PASS")
            if edk2_name:
                gaddr = l0_blocks["edk2"] if preview else edk2_addr
                laddr = l0_blocks["layer"] if preview else layer_addr
                gtool = ensure_dump_tool(bundle, args.gfdt_dump, "gfdt_dump")
                rdopt = (rd_addr, rd.range_size) if rd is not None else None
                if rdopt and not tool_has_option(gtool, "--ramdisk"):
                    say("NOTE: this gfdt_dump has no --ramdisk option (T21a adds it): the carve of the ramdisk out of /memory is "
                        "NOT previewed here; the layer's own FATAL line is the backstop. The check below covers the EDK2 placement only.")
                    rdopt = None
                try:
                    grc, _ = run_gfdt(gtool, path, laddr, gaddr,
                                      struct.unpack_from("<Q", edk2, 16)[0], args.fb, gran_from_args(args.args), rdopt)
                except OSError as e:
                    say("gfdt_dump could not be run (%s: %s): NOT jumping." % (type(e).__name__, e))
                    return 1
                if grc == 3:
                    say("GUEST FDT CHECK: FAIL - privacy self-check hit; do not use or share this output.")
                    return 1
                if grc not in (0, 1):
                    say("GUEST FDT CHECK: ERROR - gfdt_dump failed with exit %d (usage error, unreadable or malformed FDT, or a "
                        "crash), which is not a verdict on the placement. NOT jumping." % grc)
                    return 1
                if grc and not target.dry_run:
                    say("GUEST FDT CHECK: FAIL - the layer's placement rule or the builder would refuse this (exit 1); the "
                        "layer would stop with a FATAL line. Nothing was jumped to.")
                    return 4
                if grc:
                    say("(dry run: gfdt_dump refused the unmodified DTB with the fake addresses, rc %d: expected)" % grc)
                else:
                    say("GUEST FDT CHECK: PASS (placement rule of the layer, builder accepted)")
        if preview:
            say("%s done: no jump was made; m1n1 is still alive on the proxy." % label)
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
    ap.add_argument("--rung", choices=sorted(RUNGS), help="L0, D0 = prepare the DT only, no jump; D0 needs --edk2 "
                    "(required except with --ramdisk-test)")
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
    ap.add_argument("--gfdt-dump", help="path of the gfdt_dump binary (default: the bundle's)")
    ap.add_argument("--edk2", help="EDK2 image, a file of the bundle (WATARI_M1_EFI.fd), for rungs D0 (preview) and D2 (jump); "
                    "loaded into its own 32 MB block, passes wel2.guest=<entry> wel2.abi=fdt wel2.gfdt=build wel2.fb=<--fb>")
    ap.add_argument("--ramdisk", metavar="IMAGE", help="Windows RAM-disk image (winram.img, with its .sha256 sidecar next to it; a path, "
                    "or a name inside the bundle folder) for rung D0/D2 with --edk2: a FOURTH block, allocated first, written in slices "
                    "with a 4 KiB trailer, passed as wel2.ramdisk=0x<base>,0x<size>")
    ap.add_argument("--ramdisk-test", type=int, metavar="MIB", help="rate test: write and read back MIB MiB in one block, print MB/s; "
                    "no DTB, no layer, no jump (first step of rung W1; --rung is not needed)")
    ap.add_argument("--ramdisk-slice", type=int, default=RD_SLICE_MIB, metavar="MIB", help="slice size of the transfer (default 16)")
    ap.add_argument("--ramdisk-verify", choices=("sample", "full", "none"), default="sample",
                    help="read-back after each slice: first and last 4 KiB (default), the whole slice, or none")
    ap.add_argument("--no-data-csums", action="store_true", help="try m1n1's Feature.DISABLE_DATA_CSUMS for the transfer [hypothesis: "
                    "API not verified; the read-back then is the only per-slice check]")
    ap.add_argument("--fb", choices=FB_MODES, default="layer",
                    help="wel2.fb=<mode>: passed through as given (layer = the layer keeps the panel; band and guest are for "
                    "the EDK2 rungs). Without --edk2, wel2.fb is added only when not 'layer'")
    ap.add_argument("--boot-timeout", type=float, default=40.0, help="seconds to wait for kboot_boot's reply (default 40, as linux.py)")
    ap.add_argument("--dry-run", action="store_true", help="no device: print every proxy call (the 'prepared' FDT is the DTB itself)")
    ap.add_argument("--go", action="store_true", help="required for rungs L1-L5 on a real device (they jump)")
    a = ap.parse_args(argv)
    if a.rung is None and a.ramdisk_test is None:
        ap.error("--rung is required (except with --ramdisk-test)")
    if a.ramdisk_test is not None and (a.ramdisk or a.rung):
        ap.error("--ramdisk-test is a mode of its own: no --rung, no --ramdisk")
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
                target.store_limit = 4 * MIB            # a 700 MB image must not be kept in host memory by the printer
            else:
                if args.rung not in ("L0", "D0", None) and not args.go:
                    raise LoadError("rung %s jumps into the layer: add --go once you mean it (try --dry-run first)" % args.rung, 2)
                if not args.m1n1:
                    raise LoadError("no m1n1 proxyclient: pass --m1n1 <checkout> or set M1N1_DIR", 2)
                prevalidate(args)                       # the ramdisk sidecar check comes BEFORE the first proxy request
                target = connect(args.m1n1, args.device)
        return run_ramdisk_test(args, target) if args.ramdisk_test is not None else run(args, target)
    except LoadError as e:
        say("ERROR: %s" % e)
        return e.code


if __name__ == "__main__":
    sys.exit(main())
