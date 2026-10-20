#!/bin/bash
# SPDX-License-Identifier: MIT
# Companion of run-m2.sh: send keys to the running Windows VM through the QEMU monitor.
#   m2-login.sh <sockdir> login          dismiss the lock screen and log in (throwaway test account of
#                                        tools/autounattend: user watari, password watari)
#   m2-login.sh <sockdir> keys K [K...]  send QEMU key names one by one (e.g. meta_l, meta_l-e, esc)
#   m2-login.sh <sockdir> shot FILE.png  screendump (via a space-free temp path, F-23) converted to PNG
# <sockdir> is the path written to <run dir>/sockdir.txt by run-m2.sh.
set -u
SD="${1:?sockdir}"; CMD="${2:?login|keys|shot}"; shift 2
mon() { printf '%s\n' "$1" | nc -U "$SD/mon.sock" -w 1 >/dev/null 2>&1; }
case "$CMD" in
login)
    mon "sendkey ctrl"; sleep 2        # wake the display (Ctrl alone does nothing in Windows)
    mon "sendkey spc"; sleep 4         # dismiss the lock screen -> password prompt
    mon "sendkey backspace"; sleep 1   # the space may land in the password field: erase it (harmless otherwise)
    for k in w a t a r i; do mon "sendkey $k"; sleep 1; done
    mon "sendkey ret" ;;
keys)
    for k in "$@"; do mon "sendkey $k"; sleep 2; done ;;
shot)
    mon "screendump $SD/m.ppm"; sleep 2
    [ -s "$SD/m.ppm" ] && sips -s format png "$SD/m.ppm" --out "$1" >/dev/null 2>&1 ;;
*) echo "usage: $0 <sockdir> login|keys|shot ..."; exit 2 ;;
esac
