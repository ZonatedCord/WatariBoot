<!-- SPDX-License-Identifier: MIT -->
# The m1n1 stub volume on Marco's M1 (read-only look, 2026-11-17)

On 2026-11-10 the Asahi installer created a 2.5 GB APFS container on the M1 (MacBookPro17,1) with the volumes `m1n1 proxy` (1.4 MB, mounted read-only by macOS) and `m1n1 proxy - Data`. We only looked at it from macOS and changed nothing. Machine-specific UUIDs are left out on purpose.

## What is in it
| Path | What it is | Use for us |
|---|---|---|
| `System/Library/CoreServices/SystemVersion.plist` | macOS **13.5 (stub)**, build 22G74 [verified] | The OS firmware version paired with this partition (see below) |
| `usr/standalone/bootcaches.plist` | `bless2` BootObjects: ANE, AOP, `Ap,DCP2` (DCP.img4), BaseSystemTrustCache, ... [verified] | Coprocessor firmware (including the display controller DCP) comes from the 13.5 set |
| `Finish Installation.app/Contents/Resources/step2.sh` | Asahi's second install step (MIT), run from this partition's recoveryOS (1TR) | Shows how iBoot is told to start our code (below) |
| `Finish Installation.app/Contents/Resources/boot.bin` | m1n1 **v1.6.1 stage 1** (`##m1n1_ver##v1.6.1`, `chosen.asahi,m1n1-stage1-version=v1.6.1`) [verified: strings] | The code iBoot starts; it falls into the USB proxy we use |
| `System/Library/CoreServices/PlatformSupport.plist` | List of Intel Macs only (board IDs `Mac-...`, `VMM-x86_64`) | None: Apple Silicon boot does not read it [hypothesis, strong] |
| `Restore` | Symlink into the Preboot volume | None |
| `m1n1 proxy - Data` | Only `private`, Spotlight and fsevents metadata | None |

## How our code gets started [verified: step2.sh]
1. `bputil -nc -v <VGID>` lowers the security policy for **this partition only** (macOS keeps Full Security). It needs the partition's own paired recoveryOS in 1TR mode.
2. `kmutil configure-boot -c boot.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v <system volume>` registers m1n1 as the partition's raw custom boot object, in place of a kernelcache.
3. iBoot then loads `boot.bin` at EL2 with the ADT and `boot_args`. That is the start of every Watari run (`TTY> Running in EL2` in the logs).

## Chainload: the way to an untethered boot
`boot.bin` contains the chainload path: `chainload=`, `chainload: NVME init failed`, `Chainloading`, `asahi,m1n1-stage2-version`, `chainload: Failed to find SEPFW` [verified: strings].

In Asahi's standard layout, stage 1 carries a `chainload=<ESP partition>;m1n1/boot.bin` variable. It initialises NVMe, reads a stage 2 from the EFI system partition and jumps to it. Without that variable it enters the USB proxy [hypothesis: known Asahi behaviour, not checked in the source]. Our stub has no such variable and no ESP, which is why every run needs Ciani's host and the USB transfer (about 70 s for the RAM disk).

**Possible next step after M3a [hypothesis, not planned yet]:** reinstall through the Asahi installer, with the option that creates an ESP, and put on that ESP:
- a stage 2 (m1n1),
- the Watari layer,
- EDK2,
- later the Windows image.

The M1 would then boot Watari on its own: no host, no tether, a faster loop. Rules that apply: only through the Asahi installer, after a Time Machine backup and Marco's confirmation; macOS and its recovery are never touched.

## OS firmware version and the display
The DCP firmware that iBoot loads for this partition is the 13.5 one. m1n1 reports it as "OS FW version". Today we only write into the framebuffer that iBoot set up (30 bpp, `x2r10g10b10`, F-44).

If we ever drive the DCP, for example to scan out an 8-bit format instead of converting a shadow buffer, the interface depends on this firmware version [hypothesis: as in Asahi's DCP driver, which supports specific firmware versions]. Changing the stub's macOS version would change it too.
