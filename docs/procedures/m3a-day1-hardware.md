# M3a day 1: safety net, Asahi "UEFI environment only", m1n1 proxy, read-only measurements (E-HW0)

Status: procedure, never run yet. Written 2026-11-10. Repository text is English; Claude talks to the people in Italian.

**People.** Marco owns the target: 13" MacBook Pro M1 (j293 / t8103). Ciani owns a second Mac (the "host Mac"): safety Mac for DFU revive and debug host for the m1n1 proxyclient.

**Goal of day 1.** **No Watari code runs on the real Mac.** Only: (1) safety net, (2) Asahi installer with "UEFI environment only" on a separate partition, (3) boot m1n1 and enter proxy mode, (4) read-only measurements (E-HW0) from Ciani's Mac, (5) write the results down.

**Why now.** Issues #40 (debug host), #55 (cluster MPIDRs, WHOAMI), #52 (SEIS, PRIbits), #58 (16 KB granule), #59 (AIC backend), #60 (E2H, CPTR/CNTHCTL), #74 (GICv2m frame alignment), Q-D2/Q-D4/Q-D5 in `docs/design/watari-el2-devirq/RISKS.md`, E-HW0 in `docs/design/watari-el2-devirq/TEST_STRATEGY.md`. Plan row: M3a in `docs/PLAN.md`.

Marking: **[verified: URL]** = read on that page while writing this document (2026-11-10); **[check]** = not verified, confirm on the spot before relying on it; **[to adapt]** = code idea whose exact API was not seen in the fetched docs.

---

## 0. Instructions for Claude running on Ciani's Mac

You (Claude Code) guide two people through this procedure. Read `CLAUDE.md` first and obey it. In addition:

1. **Language and pace.** Talk to the person in Italian. One step at a time: say what the step does, who does it (Marco / Ciani / you), and wait for an explicit "done" or the output before the next step. Never batch steps across a hardware action (cable, power button, recovery).
2. **Never write firmware, NVRAM, boot policy or partitions on the target Mac.** The only exception is the official Asahi installer (and, only if section 3.3 option B is chosen, the two 1TR commands listed there), run by Marco on his own Mac, **after** Marco has said an explicit "go" in this chat for that step and has confirmed a fresh Time Machine backup. You never type commands on Marco's Mac; you tell him what to type.
3. **DFU.** Only **Revive** first. **Restore erases everything** on the target: only with Marco's explicit consent in chat, after Revive failed, and after telling him what will be lost.
4. **Backup rule, per machine.**
   - Marco's Mac: the project backup rule applies in full (Time Machine, confirmed, fresh, before the installer).
   - **Ciani's Mac: Ciani waived the backup for his own Mac.** That is why the **zero-footprint rule** below replaces it.
5. **Zero-footprint rule for Ciani's Mac.**
   - Everything lives in ONE folder, `~/WatariBoot-host/` (`$HOSTDIR`). Deleting it with one `rm -rf` must leave nothing behind, except the items listed in section 7.2, which you clean one by one.
   - No Homebrew. No `pip install` outside the venv. No `sudo`. No .pkg or DMG installs. No edits to `~/.zshrc`, `~/.zprofile`, `~/.gitconfig` or any dotfile.
   - Python venv **inside** `$HOSTDIR`. `PIP_CACHE_DIR`/`PIP_NO_CACHE_DIR`, `PYTHONPYCACHEPREFIX`/`PYTHONDONTWRITEBYTECODE`, `TMPDIR` and `XDG_CACHE_HOME` point inside `$HOSTDIR`, set by `$HOSTDIR/env.sh`, which is **sourced per shell session** (never from a dotfile). Every command you run on Ciani's Mac starts with `source ~/WatariBoot-host/env.sh`.
   - m1n1 comes as a source archive (curl + unzip) or a git clone **into** `$HOSTDIR`.
   - **Command Line Tools (CLT) check before anything else** (section 2.2). CLT is a system-wide install outside the folder. On a stock Mac `/usr/bin/python3` and `/usr/bin/git` are shims that pop up the CLT installer when run, so **do not run them to see whether they exist**; use `xcode-select -p` and `ls`. If CLT is missing, stop and ask Ciani explicitly (install CLT, or use the standalone Python alternative).
   - Record every file or tool created or installed on Ciani's Mac in `$HOSTDIR/INSTALLED-host.md` (what, where, how to remove) and copy its content into the session diary before cleanup. Create the timestamp marker `$HOSTDIR/.start-marker` first and copy it to `/tmp/watari-host-marker` too (used by the checks in section 7, because the folder itself will be deleted).
   - Anything that would write outside `$HOSTDIR` (apart from the repository checkout this session already runs from): stop and ask.
6. **proxyclient is read-only on day 1.** Register and memory **reads** and ADT inspection only. No `p.write*`, `p.set*`, `p.clear*`, `p.mask*`, no `u.msr(...)`, no `chainload`, no `linux.py`, no `run_guest*`, no hypervisor, no `smp_start_secondaries` unless section 5 row 2 says both people agreed. Never read the AIC `EVENT` register (offset `0x2004`): the read **masks** the delivered source (a side effect). Read only addresses that an ADT node lists in its `reg`. Show every command before running it; ask the person to confirm any command that is not literally in this document.
7. **Stop and ask** if anything is unexpected: a different screen, an unexplained error, a USB device name that differs from this document, a missing ADT node, a value that contradicts the table in section 5 (do not "fix" it, record it).
8. **At the end:** write the results into `re/m3a-ehw0-measurements.md` (template in 5.3) and a session diary `docs/sessions/YYYY-MM-DD-<author>-NN.md`; update `docs/STATUS.md` only in the end-of-session PR (`docs/CONTRIBUTING.md` section 4); branch `ciani/m3a-day1-ehw0`, one PR to `main`, never push to `main`; run `python3 -I tools/token-usage.py --autore ciani`. Then run section 7.

---

## 1. Prerequisites and safety (both people)

### 1.1 Backups
- **Marco's Mac:** a Time Machine backup finished **today** and verified (open Time Machine, check the date). Do not start section 3 without it. Mains power, full battery. The Asahi tethered-boot page requires an Apple Silicon Mac on at least macOS 12.3 with a password-protected administrator account **[verified: https://asahilinux.org/docs/sw/tethered-boot/]**.
- **Ciani's Mac:** backup waived by Ciani; the zero-footprint rule (section 0) applies instead.

### 1.2 Cable
- A **USB-C to USB-C cable that supports data and charging**; Apple: "Don't use a Thunderbolt 3 cable" **[verified: https://support.apple.com/108900]**.
- How to tell a data cable: a charge-only cable shows no DFU window and no `usbmodem` device. Test on the spot [check]: with both Macs on, connect them and look at System Information > USB on the host. Use a short cable, no hub.
- For the m1n1 proxy: a USB Type C cable, on the Mac use a Thunderbolt port; C-to-A also works with the A end on the host; connect a charger to another port of the target **[verified: https://asahilinux.org/docs/sw/tethered-boot/]**.

### 1.3 DFU safety net (Finder first, Apple Configurator only as fallback)
Apple documents that the host can be "another Mac of any model using macOS 14 or later" and that revive/restore is done "from the DFU window in the Finder" **[verified: https://support.apple.com/108900]**. **Finder is therefore the primary method; Apple Configurator is NOT installed unless the Finder shows no DFU window.** (Apple's Configurator page for the same task, https://support.apple.com/en-euro/guide/apple-configurator-mac/revive-or-restore-a-mac-with-apple-silicon-apdd5f3c75ad/2.17/mac/14.0 , only points back to article 108900 for the steps.) The host Mac needs about 60 GB of free storage and internet access.

Facts from https://support.apple.com/108900 **[verified]**:
- Plug the cable into the affected Mac's **DFU port**; keep other USB ports empty.
- Laptop key sequence: (1) press and hold the power (Touch ID) button up to 10 seconds until the Mac turns off; (2) press and release the power button, then immediately press and hold Left Control, Left Option, Right Shift and the power button together; (3) hold all four for about 10 seconds, then release all except the power button; (4) keep holding the power button up to 10 more seconds until the host shows a DFU window in the Finder.
- "From the DFU window in the Finder, click Revive Mac. Then click Continue." Revive does not erase the Mac. "Restore Mac" erases the Mac and returns it to factory settings.
- If no DFU window appears: Finder > Settings > Sidebar, "CDs, DVDs, and iOS Devices" must be selected; look for "Mac" in the Locations section of the sidebar; retry with correct key timing and a different data cable connected directly to both Macs.

DFU port, from https://support.apple.com/120694 **[verified]**: the general rule is "the DFU port is the type USB-C port farthest to the left (leftmost) or farthest to the right (rightmost)"; for MacBook Pro with Apple silicon, "All other models: The leftmost USB-C port when you're facing the left side of the Mac". That page does not name the 13" M1 MacBook Pro explicitly (it falls under "All other models", an inference), so **[check]** physically which of the two left-side ports Apple means; if the host sees nothing, try the other port (Apple's port pages have been reported wrong for other models; no report was found for this one).

**Recommended dry run [check]:** before the installer, Marco enters DFU once and Ciani confirms that the Finder shows the DFU window, **without clicking Revive or Restore**. This proves cable, port and key sequence before they are needed. How to leave DFU without reviving (hold the power button until the Mac restarts) is not stated on the Apple page: [check].

Apple Configurator (Mac App Store, free) is the fallback only. It is the one thing that weakens the zero-footprint rule (an app in `/Applications` plus data under `~/Library`); install it only if needed, record it in `INSTALLED-host.md`, remove it with section 7.2.

---

## 2. Ciani's Mac: host setup (zero footprint)

Each download below is a download on Ciani's Mac: Ciani confirms it first (backup waived, zero-footprint rule in force).

### 2.1 Folder and environment file
```sh
mkdir -p ~/WatariBoot-host/{tmp,cache,pyc,work}
touch ~/WatariBoot-host/.start-marker
cp -p ~/WatariBoot-host/.start-marker /tmp/watari-host-marker
cat > ~/WatariBoot-host/env.sh <<'ENVEOF'
# Sourced per shell session only. Never add this to ~/.zshrc.
export HOSTDIR="$HOME/WatariBoot-host"
export TMPDIR="$HOSTDIR/tmp"
export PIP_CACHE_DIR="$HOSTDIR/cache/pip"
export PIP_NO_CACHE_DIR=1
export PIP_DISABLE_PIP_VERSION_CHECK=1
export PYTHONPYCACHEPREFIX="$HOSTDIR/pyc"
export PYTHONDONTWRITEBYTECODE=1
export XDG_CACHE_HOME="$HOSTDIR/cache"
export HISTFILE=/dev/null            # keep this session's commands out of ~/.zsh_history
[ -d "$HOSTDIR/venv" ] && source "$HOSTDIR/venv/bin/activate"
ENVEOF
echo "- ~/WatariBoot-host (whole folder), created $(date). Remove: rm -rf ~/WatariBoot-host" > ~/WatariBoot-host/INSTALLED-host.md
```
`HISTFILE` affects only the current shell; Terminal.app may still write `~/.zsh_sessions/` [check]; section 7.2 lists it.

### 2.2 Python and git: check WITHOUT triggering the CLT installer
Do **not** run `python3` or `git` yet. Run:
```sh
xcode-select -p; echo "exit=$?"
ls /Library/Developer/CommandLineTools/usr/bin/python3 /Library/Developer/CommandLineTools/usr/bin/git 2>&1
ls /opt/homebrew/bin/python3 /usr/local/bin/python3 2>&1     # only look, install nothing
```
Decision:
- **CLT present** (`xcode-select -p` prints a path and the files exist): create the venv with `/Library/Developer/CommandLineTools/usr/bin/python3`. m1n1's `requirements.txt` was generated with Python 3.14 **[verified: raw requirements.txt]**; the Asahi tethered-boot page asks for Python 3.9 or later **[verified]**. Whether the CLT Python works with the pinned versions is [check].
- **A Python Ciani already had** (python.org or Homebrew installed earlier): fine to use; install nothing new.
- **CLT missing and no other Python: STOP.** Ask Ciani explicitly, in Italian, to choose: (a) install the Command Line Tools (system-wide, outside the folder; removing them needs `sudo rm -rf /Library/Developer/CommandLineTools`, [check] receipts); or (b) a standalone Python: a prebuilt CPython `aarch64-apple-darwin` archive from the `python-build-standalone` project (https://github.com/astral-sh/python-build-standalone/releases , [check] exact asset name and SHA-256 on the release page), downloaded with `curl -L -o "$HOSTDIR/work/<asset>"`, extracted inside `$HOSTDIR`, used for the venv. (b) keeps the zero-footprint rule. Do not choose for him.
- **git is not needed** for this procedure (m1n1 comes as an archive). The repository checkout Claude runs from already exists. If results must be committed and `git` is unusable without CLT, write the result files and let someone with git commit them; tell the user.

### 2.3 m1n1 as a source archive, into the folder
```sh
source ~/WatariBoot-host/env.sh
cd "$HOSTDIR/work"
curl -L -o m1n1.zip https://github.com/AsahiLinux/m1n1/archive/refs/heads/main.zip
shasum -a 256 m1n1.zip      # note it in INSTALLED-host.md; pin a tag/commit instead of main if one is agreed [check]
unzip -q m1n1.zip
```
- Source: https://github.com/AsahiLinux/m1n1 ; the repo root has `proxyclient/` and `requirements.txt` **[verified]**.
- m1n1's ABI is unstable (Asahi hypervisor page: chainload a build from the same tree so the proxyclient matches **[verified: https://github.com/AsahiLinux/docs , docs/sw/m1n1-hypervisor.md]**). Day 1 does **not** chainload, so the proxyclient must be compatible with the m1n1 stage the installer puts on the Mac: record both versions in the results [check].
- We do **not** build m1n1 (needs a cross compiler or LLVM and ImageMagick: more installs). The m1n1 on the Mac is the one the Asahi installer installs.

### 2.4 Python venv inside the folder
```sh
source ~/WatariBoot-host/env.sh
<python3-chosen-in-2.2> -m venv "$HOSTDIR/venv"
source ~/WatariBoot-host/env.sh        # re-source: now activates the venv
cd "$HOSTDIR/work/m1n1-main"
pip install -r requirements.txt        # asn1, construct, enum-compat, pyserial [verified: raw requirements.txt]
pip config list; pip cache dir         # both must point inside $HOSTDIR (or be empty)
```
Append the venv and the m1n1 folder to `INSTALLED-host.md`. (The tethered-boot page lists `pyserial construct serial.tool` **[verified]**; `serial.tool` is part of pyserial; the repo `requirements.txt` is the more current list.)

---

## 3. Marco's Mac: Asahi installer, "UEFI environment only"

**Run only after: fresh Time Machine backup confirmed by Marco in chat, and Marco's explicit "go" for this step.** This is the only step of day 1 that changes the target's disk and boot configuration.

### 3.1 What the installer does (Asahi docs)
- Entry point on the project home page: `curl https://alx.sh | sh` ("Install from macOS") **[verified: https://asahilinux.org/]**. Fedora Asahi Remix has its own `curl https://fedora-asahi-remix.org/install | sh` **[verified: https://fedora-asahi-remix.org/]**; for a UEFI-only environment use the Asahi one [check which script currently offers the option]. Piping into `sh` runs the script unseen: save it first (`curl -L -o /tmp/alx.sh https://alx.sh`), read it with Marco, then run it [check that the bootstrap works from a saved file].
- The installer is a Python tool, online-only, launched from macOS or recoveryOS, streaming parts of the public IPSW images; "Users can instead install only a UEFI boot environment and leave unpartitioned space for another OS installer" **[verified: https://asahilinux.org/docs/platform/open-os-interop/]**. A web search also mentions a minimal UEFI-only environment and a forum post suggesting the option may be absent from some installer versions [check: exact menu wording and availability in the installer Marco runs; read the menu aloud, do not guess].
- Layout per installed OS **[verified: open-os-interop and https://asahilinux.org/docs/sw/partitioning-cheatsheet/]**: an APFS "stub" container of about 2.5 GB (iBoot2, firmware, recoveryOS; **m1n1 stage 1** as its kernel), a FAT32 EFI system partition of about 500 MB (m1n1 stage 2, device trees, U-Boot, a loader), and for a full distro an ext4 root. With "UEFI environment only" there is no distro root: free space is left unpartitioned.
- Boot chain: iBoot2 -> m1n1 stage 1 -> m1n1 stage 2 -> U-Boot -> loader **[verified: open-os-interop]**. Stage 1 can only be changed from 1TR, so the installer sends the user into recovery for the last step (the installer's own prompts; details [check]).
- The macOS APFS container is shrunk to make room (the cheatsheet's "growing macOS back" uses `diskutil apfs resizeContainer`, **[verified]**). Existing macOS data are kept but moved/resized: hence the backup.

### 3.2 Partition size
The Asahi pages fetched give **no** recommended size for the UEFI-only option **[check: not found]**; the installer asks how much space to give and shows what it needs. Day 1 needs no OS space. Rule: give the minimum the installer accepts plus the free space it leaves "for another OS"; this can be re-assigned later (section 6.1). Decide with Marco on the spot, write the sizes into the results. Later (M3b) a Windows ARM64 disk and the Watari image will need real space: Marco's plan.

### 3.3 Getting m1n1 into proxy mode
Facts **[verified: https://asahilinux.org/docs/sw/m1n1-user-guide/ and https://asahilinux.org/docs/sw/tethered-boot/]**:
- Proxy mode is a USB device interface on all Thunderbolt ports (C end on the target).
- m1n1 falls back to proxy mode when no payloads are given or booting them fails.
- Release builds: "backdoor" proxy mode needs verbose mode (`nvram boot-args=-v`) and SIP disabled for that OS (`csrutil disable`); then m1n1 waits 5 seconds before booting payloads and a USB proxy connection in that window enters proxy mode. In 1TR: Options, Utilities, Terminal, run `csrutil disable && nvram boot-args=-v`, select the Asahi volume and authenticate, shut down. A "pairing error" means the default boot OS does not match the OS being modified.
- In the installer's expert mode "you can choose tethered-only proxy mode" (tethered-boot page).

Decide with Marco (this choice touches NVRAM and security policy, so it needs his explicit consent):
- **Option A (preferred): installer expert mode, tethered-only proxy** [check exact wording]. Nothing is typed by hand.
- **Option B (fallback): backdoor mode.** Marco himself types, in 1TR Terminal, selecting the Asahi volume (the doc selects "the Asahi Linux volume", not macOS): `csrutil disable && nvram boot-args=-v`. Undo at the end: `nvram boot-args=` and `csrutil enable`; **do not choose Full Security when prompted: that uninstalls m1n1** **[verified: m1n1-user-guide]**. Claude never types these.

### 3.4 Booting the new volume (boot picker)
- Hold the power button from a shut-down state until startup options appear **[verified: tethered-boot]**; pick the Asahi/m1n1 volume. If Startup Options is unreachable, a double tap-hold of the power button is suggested **[verified: partitioning-cheatsheet]**.
- At the end of day 1 macOS must be the default boot OS again (System Settings, or Option-click at the picker) **[verified: partitioning-cheatsheet]**, unless Marco decides otherwise.

---

## 4. Connect and open the proxy (Ciani's Mac)

1. Cable: C-to-C data cable from a Thunderbolt/USB-C port of the target to the host; charger on another target port; no hub.
2. Host, read-only: `ls /dev/cu.usbmodem* 2>&1`. On macOS m1n1 appears as `/dev/cu.usbmodemP_01` and `/dev/cu.usbmodemP_03`; the first is the proxy interface, the second is reserved for the hypervisor's virtual UART **[verified: m1n1-user-guide]**. Exact names on this host: [check].
3. `export M1N1DEVICE=/dev/cu.usbmodemP_01` (the doc says `M1N1DEVICE` selects the device **[verified]**), in the session or in `$HOSTDIR/env.sh` (inside the folder, fine).
4. Boot the target (Marco, picker, Asahi volume). In backdoor mode the 5-second window starts at boot and the host opening the device triggers proxy mode **[verified]**: the host must be ready. The tethered-boot page uses `proxyclient/tools/picocom-sec.sh` for the second port and needs `picocom` (another install): **skip it**, day 1 does not need the second port.
5. Verify: in `$HOSTDIR/work/m1n1-main`, `python3 proxyclient/tools/shell.py` opens an interactive Python shell, exit with Ctrl-D **[verified: tethered-boot]**. For repeatability Claude writes the read-only script to `$HOSTDIR/work/ehw0.py` and runs it from that directory with the venv Python [check import path; do not use `python3 -I` here, it ignores `PYTHONPATH`]. No `chainload.py`, `linux.py`, `run_guest*.py` on day 1.

No `usbmodem` device: charge-only cable, wrong port, outside the 5-second window, or not a Thunderbolt port. Change nothing on the target; power off, retry; if still stuck, stop and record.

---

## 5. E-HW0 measurements (read-only)

Prerequisite: the proxy shell works (section 4). Snippets assume `from m1n1.setup import *`, which exposes `p` (proxy), `u` (utils) and `mon` **[verified in our note `re/f0-timer-irq-hw.md` from the m1n1 source `proxyclient/m1n1/setup.py`]**; `u.mrs(...)` reads a system register (same note, e.g. `u.mrs(CNTFRQ_EL0)`), `p.read32(addr)` reads memory; a register without a Python constant is addressed with the tuple `(op0, op1, CRn, CRm, op2)`. **All snippets are [to adapt]**: the Asahi pages fetched here do not document this API; check against `$HOSTDIR/work/m1n1-main/proxyclient/`.

**Caveat for every sysreg:** the value read is what iBoot/m1n1 left, not necessarily the CPU reset value (m1n1 runs at EL2 and sets HCR_EL2, CPTR_EL2, CNTHCTL_EL2 and others itself). Record it as "as handed over by m1n1 <version>"; for true reset values read what m1n1 writes [check].

**Memory reads** only at addresses an ADT node lists in `reg`. A read of an unmapped or clock-gated address can fault; the Mac then shows an exception in the proxy or reboots; a power cycle recovers. Never read AIC `EVENT` (`0x2004`).

### 5.0 Dump the ADT first
```python
# [to adapt]
from m1n1.setup import *
print(u.adt)       # redirect to $HOSTDIR/work/adt-j293.txt
```
Keep the full dump in `$HOSTDIR/work/` and copy only the nodes below into `re/m3a-ehw0-measurements.md`. **Before committing, remove serial numbers, MAC addresses and other identifiers** (grep for `serial`, `mac-address`, `wifi`, `bluetooth`) [check].

### 5.1 Measurements

| # | What we read | Settles | Expected in our code | Snippet idea [to adapt] |
|---|---|---|---|---|
| 1 | AIC node (`/arm-io/aic`, name [check]) `reg` base; `INFO` at offset `0x0004`, NR_IRQ in bits [15:0] | Q-D4 (896?), INFO field widths, #59 | base `0x23b100000`, window `0x8000`, NR_IRQ 896 [hypothesis] (`el2/include/wel2_aic.h`: `AIC_INFO_NR_IRQ`, `AIC_MAX_IRQS`) | `aic = u.adt["/arm-io/aic"].get_reg(0)[0]` [check]; `info = p.read32(aic + 0x4)`; print `hex(aic), hex(info), info & 0xffff` and the full 32 bits |
| 2 | `WHOAMI` (offset `0x2000`) on each core next to that core's `MPIDR_EL1` | #55, R-D5 (WHOAMI vs MPIDR of P cores) | E0-3 = 0-3, P0-3 = 4-7 [hypothesis] (`pirq_aic_init_cpu`, `cpu_of_vcpu`) | Boot core: `u.mrs(MPIDR_EL1)` and `p.read32(aic + 0x2000)`. Other cores need `p.smp_start_secondaries()` and `p.smp_call_sync(cpu, addr, ...)` with a code blob (`re/f0-timer-irq-hw.md`: how to run an `mrs` on another core is **not verified**). Starting secondaries is not a pure read and is not persistent: **skip unless Marco and Ciani both agree**; record "boot core only". The ADT `cpu` nodes (`reg`, `cpu-id`, cluster properties [check]) already give the MPIDR layout without running code on other cores |
| 3 | `ICH_VTR_EL2`: ListRegs, PRIbits, PREbits, SEIS, TDS | #52 (SEIS erratum), #60 (PRI/PRE widths), F-19 | SEIS = 1 expected on M1 [hypothesis, F-19]; widths unknown; LR count vs the layer | `u.mrs((3,4,12,11,1))` [check encoding S3_4_C12_C11_1]; decode ListRegs [4:0], PREbits [28:26], PRIbits [31:29], SEIS bit 22, TDS bit 19 (GIC architecture spec, [check] against the layer's decode). Apple's M1 may not implement GICv3 `ICH_*` at all: **if the read raises an exception, that is the answer**; record it, do not loop, and note that #52 then needs another approach |
| 4 | `HCR_EL2` (bit 34 = E2H) | #60 (E2H stuck at 1), `wel2.e2h` | E2H = 1 [hypothesis] | `hex(u.mrs(HCR_EL2))`. The "write 0, read back" stuck-bit test is a write: **day 2** |
| 5 | `ID_AA64MMFR0_EL1` (PARange, TGran4/16/64, TGran4_2/16_2/64_2), `ID_AA64MMFR1_EL1` (VH), `ID_AA64PFR0_EL1` | #58, #60 | TGran4 = 0b1111 (unsupported), TGran16 supported (`el2/boot/README.md`); PARange < 40 bits [hypothesis] (code uses `min(PARange, 40)`) | `v = u.mrs(ID_AA64MMFR0_EL1)`; PARange [3:0], TGran16 [23:20], TGran64 [27:24], TGran4 [31:28], TGran16_2 [35:32], TGran64_2 [39:36], TGran4_2 [43:40] (Arm ARM, [check]) |
| 6 | `CPTR_EL2`, `CNTHCTL_EL2` | #60 | code writes the CPACR layout (FPEN = 0b11) with E2H = 1; handed-over values unknown [hypothesis] | `u.mrs(CPTR_EL2)`, `u.mrs(CNTHCTL_EL2)` [names check]; record the m1n1 version |
| 7 | Physical map around `0xFFFFC000-0xFFFFFFFF` | #74, Q-D2 | `0xFFFFF000` free [hypothesis]; GICv2m frame at `0xFFFFF000`, doorbell `0xFFFFF040` (`el2/vgic/v2m.c`); the 16 KB hole would be `0xFFFFC000-0xFFFFFFFF` | Walk the ADT: every `reg` (address, size) of every node plus `/chosen/memory-map` and carveouts; print each range overlapping `[0xFFFFC000, 0x100000000)`. **Do not read the address itself.** Then decide: move the frame (16 KB aligned, free, below 4 GiB) or emulate the neighbours |
| 8 | `CNTFRQ_EL0` | timer assumptions | 24000000 (from XNU/Asahi sources per `re/f0-timer-irq-hw.md`; confirm here) | `u.mrs(CNTFRQ_EL0)` |
| 9 | `MIDR_EL1` of an E core and a P core | #55 | record, do not assume | Boot core: `u.mrs(MIDR_EL1)` and note which type it is (from MPIDR / ADT). The other type needs a secondary core (row 2) or an ADT property [check]; otherwise record "one type only" |
| 10 | ADT ANS node(s): `interrupts`, `reg` | interrupt table | ANS `590`, mailbox `583` denied [hypothesis] (`el2/vgic/intr_map.c`) | nodes named `ans*` / `nvme*`; print `interrupts` |
| 11 | ADT DWC3 / USB node(s): `interrupts` | interrupt table | DWC3 `777`, `857` | nodes `usb-drd*`, `dwc3*` [names check] |
| 12 | ADT PCIe nodes (`apcie*` [check]): `interrupts`, every property whose name contains `msi`, `reg` | interrupt table, E-HW1 prerequisites (#68) | ports `695`, `698`, `701` | print those properties |
| 13 | ADT UART node(s): `reg`, `interrupts`, clock | Apple UART driver for the layer | base unknown; the Linux t8103 DT uses `0x235200000` for uart0 [hypothesis, confirm in the dump] | nodes `/arm-io/uart*` [check] |
| 14 | Safe AIC reads: `HW_IRQ_MON` (`0x4200`), `MASK_SET` as state (`0x4100`), `TARGET_CPU` (`0x3000 + 4n`) for a few n | handed-over values (Q-D5 itself needs writes: day 2) | meanings are [hypothesis] in the header | `p.read32(aic + 0x4200)` etc.; a few words only; **never `0x2004`** |

Also record: macOS version on the target, m1n1 version/hash installed by the installer, host Mac model/macOS, cable, and how long the 5-second window really lasted.

### 5.2 Deliberately NOT done on day 1 (list them in the results as "deferred")
Any write (E2H stuck-bit test, AIC `SW_SET`/mask/target experiments for Q-D5 edge latch and spurious events, `TARGET_CPU` routing), PMGR/clock access, DART, PCIe port bring-up (E-HW1, #68), starting secondary cores (unless agreed), hypervisor, chainload, any payload.

### 5.3 Result file template: `re/m3a-ehw0-measurements.md`
Header: date, who, hardware (j293 / t8103), macOS version, m1n1 version, host Mac and macOS, cable. One section per row of 5.1 with: raw value (hex), decoded fields, "matches our code: yes / no / n.a.", and the mark `[verified: measured on j293 on <date>, m1n1 <version>]`. Final section "Hypotheses settled / still open" naming #52 #55 #58 #59 #60 #74, Q-D2, Q-D4. No raw dump with serial numbers or MAC addresses. The PR description lists the issues the results inform; no per-issue comments (notification rule, `docs/CONTRIBUTING.md` section 5).

---

## 6. Undo, and what to do when something goes wrong

### 6.1 Undo the Asahi install (Marco's Mac)
From https://asahilinux.org/docs/sw/partitioning-cheatsheet/ **[verified]**:
1. Set macOS as the default boot OS again (System Settings, or hold Option while selecting it in Startup Options; double tap-hold of the power button if Startup Options is unreachable).
2. If backdoor mode was used, undo it first (`nvram boot-args=`, `csrutil enable`, not Full Security).
3. `diskutil list` to find the numbers (they differ from the doc's examples). Delete the stub: `diskutil apfs deleteContainer <diskN or disk0sM>`. Delete the EFI (and any Linux) partition: `diskutil eraseVolume free free <disk0sK>`.
4. Give the space back: `diskutil apfs resizeContainer disk0s2 0` (or the macOS container's id; `0` fills the free space after it). If it fails with "Storage system verify or repair failed": boot recoveryOS, `diskutil repairVolume disk0s2`; unlock FileVault data volumes with `diskutil apfs unlockVolume <disk>` if reported; retry.
5. **Never delete the `Apple_APFS_Recovery` partition or the first (iBoot System Container) partition**; create/remove partitions only between them. If System Recovery is broken the doc says to use Apple Configurator on another Mac, or idevicerestore, for a DFU restore.
Claude reads these aloud; Marco types; nothing runs before his "go".

### 6.2 If something goes wrong (in this order)
1. Mac boots the wrong OS or sits at a picker: hold the power button to reach startup options, pick macOS, set it as default.
2. macOS does not boot: boot picker, then recovery (power button held at start, Options); use 6.1 from there, or reinstall macOS from recovery.
3. No picker, black screen or a boot loop: DFU **Revive** from Ciani's Mac through the **Finder** (1.3). Revive does not erase **[verified: Apple 108900]**.
4. Revive fails: retry once with another cable/port; then **Restore** only with Marco's explicit consent after reading him "Restore erases the Mac and returns it to factory settings" **[verified]**. Last resort; the Time Machine backup is the way to get the data back.
5. The Finder never shows the DFU window: Apple Configurator on Ciani's Mac (1.3, cleanup in 7.2).
6. Anything else: stop and report; do not improvise.

---

## 7. Cleanup on Ciani's Mac

### 7.1 Remove everything in the folder
```sh
cd ~                                         # not inside the folder
cat ~/WatariBoot-host/INSTALLED-host.md      # paste into the session diary BEFORE deleting
ls -la ~/WatariBoot-host                     # review what goes away; results already copied into the repo checkout
deactivate 2>/dev/null; true
# Ciani confirms, then:
rm -rf ~/WatariBoot-host
rm -f /tmp/watari-host-marker                # only AFTER the checks in 7.3
```

### 7.2 Places outside the folder that something could still write to
Check each; clean only what this session created.

| Place | Why | Check / clean |
|---|---|---|
| `~/.zsh_history`, `~/.zsh_sessions/` | the shell may save commands despite `HISTFILE=/dev/null` (Terminal.app session restore [check]) | `ls -lt ~/.zsh_history ~/.zsh_sessions 2>/dev/null \| head`; remove only this session's lines/files, with Ciani's approval |
| `~/Library/Caches` | macOS and apps (Terminal, Finder, a CLT Python framework) may add entries | `find ~/Library/Caches -maxdepth 2 -newer /tmp/watari-host-marker`; delete only entries clearly ours (`pip`, `python`, `org.python*`); Apple's own cache entries are normal |
| `~/Library/Preferences`, `~/Library/Saved Application State` | Terminal/Finder preferences; the Finder sidebar toggle "CDs, DVDs, and iOS Devices" if enabled for DFU | `find ~/Library/Preferences -maxdepth 1 -newer /tmp/watari-host-marker`; revert the sidebar toggle by hand if Ciani changed it |
| Finder / device-restore firmware cache (IPSW downloaded by Revive) | Revive/Restore downloads firmware; macOS keeps a cache (Apple asks for ~60 GB free); location not documented in the pages fetched **[check]** | `find ~/Library -newer /tmp/watari-host-marker -size +100M 2>/dev/null`; `du -sh ~/Library/iTunes 2>/dev/null`; delete only firmware files created today |
| Apple Configurator (only if installed as fallback) | app in `/Applications`, data in `~/Library` | quit it, move `/Applications/Apple Configurator.app` to the Trash; then `ls -d ~/Library/Containers/*onfigurator* ~/Library/Group\ Containers/*onfigurator* ~/Library/Preferences/*onfigurator* ~/Library/Caches/*onfigurator* 2>/dev/null` and remove those that exist [check names]; empty the Trash |
| Command Line Tools (only if Ciani agreed to install them) | system-wide `/Library/Developer/CommandLineTools` | `sudo rm -rf /Library/Developer/CommandLineTools`, then `sudo xcode-select --reset` [check]; Ciani decides |
| macOS "Allow accessories to connect" decision | macOS may store a trust decision for the USB device | System Settings > Privacy & Security; revert if changed [check] |
| `~/.claude/` (Claude Code's own state), the repo checkout | exist because Claude Code runs there | not part of this procedure; Ciani decides |
| Unified log, `/dev/cu.usbmodem*` | USB attach events are logged; the devices exist only while connected | nothing to clean |

### 7.3 Verification
```sh
ls -d ~/WatariBoot-host 2>&1                                      # expect: No such file or directory
grep -n "WatariBoot-host" ~/.zshrc ~/.zprofile ~/.zshenv ~/.bash_profile 2>/dev/null   # expect: nothing
ls ~/Library/Caches/pip 2>&1                                      # expect: No such file or directory
find ~/Library/Caches ~/Library/Preferences -maxdepth 2 \( -iname '*pip*' -o -iname '*python*' -o -iname '*m1n1*' \) -newer /tmp/watari-host-marker 2>/dev/null   # expect: nothing
find ~ -maxdepth 3 -newer /tmp/watari-host-marker -not -path "$HOME/Library/*" -not -path "$HOME/.claude/*" -not -path "$HOME/.Trash/*" 2>/dev/null | head -30   # review, expect only the repo checkout files
which pip python3                                                 # must not point into the deleted folder
```
Claude reports each result to Ciani in Italian and lists anything unexpected; it deletes nothing unknown.

---

## 8. Checklist summary

**Before the day**
- [ ] Marco: Time Machine backup finished today and verified; mains power; admin account. Ciani: backup waived, zero-footprint rule understood.
- [ ] USB-C **data** cable (not a Thunderbolt 3 cable, per Apple); a charger for the target.
- [ ] Claude on Ciani's Mac has read section 0 and `CLAUDE.md`.

**Ciani's Mac**
- [ ] `~/WatariBoot-host/`, `env.sh`, marker (also in `/tmp`) (2.1).
- [ ] CLT/Python decision made explicitly; no `/usr/bin/python3` or `/usr/bin/git` run before the check (2.2).
- [ ] m1n1 archive and venv inside the folder; `INSTALLED-host.md` filled (2.3, 2.4).
- [ ] DFU path known: Finder shows the DFU window (dry run), or the Configurator fallback is decided (1.3).

**Marco's Mac**
- [ ] Explicit "go"; installer run; "UEFI environment only" chosen; sizes noted; proxy method A or B decided (3).
- [ ] Boots the Asahi volume from the picker (3.4); macOS still works and is the default at the end.

**Measurements (read-only)**
- [ ] `/dev/cu.usbmodemP_01` visible, `M1N1DEVICE` set, `shell.py` opens (4).
- [ ] Rows 1-14 recorded as measured / not possible / deferred (5).
- [ ] No write, no AIC `EVENT` read, no chainload.

**End**
- [ ] Backdoor mode undone if used; macOS default restored.
- [ ] `re/m3a-ehw0-measurements.md`, session diary (with the host `INSTALLED-host.md` content), branch `ciani/m3a-day1-ehw0`, one PR, token counter.
- [ ] Section 7 cleanup run and verified; Ciani confirms.
- [ ] Marco decides: keep the Asahi partition for day 2 (writes, E-HW1) or remove it (6.1).

---

## 9. Sources
Apple: https://support.apple.com/108900 (revive/restore, Finder method, key sequence, requirements); https://support.apple.com/120694 (DFU port); https://support.apple.com/en-euro/guide/apple-configurator-mac/revive-or-restore-a-mac-with-apple-silicon-apdd5f3c75ad/2.17/mac/14.0 (Configurator page, points to 108900).

Asahi: https://asahilinux.org/ (installer command); https://asahilinux.org/docs/ (index); https://asahilinux.org/docs/platform/open-os-interop/ (UEFI-only, layout, boot chain); https://asahilinux.org/docs/sw/partitioning-cheatsheet/ (layout, removal, resize, recovery); https://asahilinux.org/docs/sw/m1n1-user-guide/ (proxy mode, device names, `M1N1DEVICE`, backdoor mode); https://asahilinux.org/docs/sw/tethered-boot/ (host setup, cable, `shell.py`); https://github.com/AsahiLinux/m1n1 and its `requirements.txt`; https://github.com/AsahiLinux/docs (hypervisor page, ABI note); https://fedora-asahi-remix.org/ (installer command only).

Not readable when written: https://docs.fedoraproject.org/en-US/fedora-asahi-remix/ (bot protection, "Access Denied"), the `asahi-installer` README (build instructions only, nothing about the options), `m1n1/proxyclient/README.md` (404), https://asahilinux.org/docs/alt/ref/ (404). Hence the [check] marks on the installer menu wording, partition size and the proxyclient API.
