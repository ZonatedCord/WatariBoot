#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""pmlog_read: parse a post-mortem log region from a raw memory dump file (a QEMU `pmemsave`, or out/pmlog.bin of
`watari-load.py --postmortem`) with the SAME parser the loader uses (watari-load.py: pmlog_parse_region).
  python3 -I tools/m3a/pmlog_read.py <dump> [-o text-file] [--boots N]
Prints a header summary on stderr and the log text on stdout (or into -o). Exit 0 ok, 5 no valid header, 6 CRC bad or inconsistent,
7 --boots given and the header's boot counter differs."""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("watari_load", os.path.join(HERE, "watari-load.py"))
wl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wl)


def main(argv):
    out = None
    boots = None
    pos = []
    i = 0
    while i < len(argv):
        if argv[i] == "-o" and i + 1 < len(argv):
            out = argv[i + 1]
            i += 2
        elif argv[i] == "--boots" and i + 1 < len(argv):
            boots = int(argv[i + 1])
            i += 2
        else:
            pos.append(argv[i])
            i += 1
    if len(pos) != 1:
        sys.stderr.write(__doc__)
        return 2
    data = open(pos[0], "rb").read()
    res = wl.pmlog_parse_region(data)
    h = res["hdr"]
    if h:
        sys.stderr.write("pmlog: status %s, version %d, capacity %d, boots %d, total %d, wraps %d, dropped %d, crc %s\n" %
                         (res["status"], h["version"], h["capacity"], h["boots"], h["total"], h["wraps"], h["dropped"],
                          "ok" if h["crc_ok"] else "BAD"))
    else:
        sys.stderr.write("pmlog: status %s\n" % res["status"])
    if res["status"] in ("short", "nomagic"):
        return 5
    if out:
        open(out, "wb").write(res["text"])
    else:
        sys.stdout.buffer.write(res["text"])
    if res["status"] != "ok":
        return 6
    if boots is not None and h["boots"] != boots:
        sys.stderr.write("pmlog: boot counter %d, expected %d\n" % (h["boots"], boots))
        return 7
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
