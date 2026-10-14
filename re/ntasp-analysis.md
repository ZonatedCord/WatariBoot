# NTASP (NT for Apple Silicon) analysis, status as of 2026-10-12

Method: read-only via `gh api` and WebFetch, no clone or download. Labels: [verified] = read directly in the source; [hypothesis] = my own deduction.
API base URL: https://api.github.com/repos/NT-for-ASi/<repo>. The site pages were read from the sources in `nt-for-asi.github.io` (commit 76abd81d, 2026-06-07).

## 1. Org repositories (org created 2023-03-27, "Bringing Windows unofficially to Apple's ARM-based devices")

| Repo | Description | Language | Declared license | Last push/commit | Contributors |
|---|---|---|---|---|---|
| m1n1_windows (fork of AsahiLinux/m1n1) | "m1n1, but adapted to run windows (aka with a GIC emulator)" | Python/C | MIT | 2026-08-25 (6dcca476, amarioguy) | 30 (marcan 1093, hoshinolina 474, ..., amarioguy 100: history inherited from the fork) |
| apple_silicon_platforms_mu | Project Mu for Apple silicon (WIP), UEFI firmware | C | BSD-2-Clause (GitHub) | 2026-09-08 (861dcc49) | 3 (amarioguy 343, JamiNeedsSleep 22, graphine27 21) |
| apple_silicon_nt_drivers | "Windows drivers and HAL Extensions" | none | none (README: MIT/GPL2) | 2025-08-28 (5fc3fe63) | 1 (amarioguy, 3 commits) |
| arm-trusted-firmware-apple | TF-A ported to Apple silicon | C | "Other"/NOASSERTION (BSD-3 of TF-A) | 2023-06-19 (d557aaec: it is a merge of upstream Arm) | 30 (all upstream Arm) |
| nt-for-asi.github.io | site (Zensical) | HTML | none | 2026-06-07 (76abd81d) | 8 (Simizfo 53, gus33000 43, Orangera1n 12, ...) |
| .github | org profile | - | - | 2026-05-09 | 1 |

[verified] `gh api orgs/NT-for-ASi/repos`, `.../commits`, `.../contributors`.

Status: the active work is in practice one person (Arminder Singh, `amarioguy`) plus a few collaborators on the site and on Mu. There are recent commits (August/September 2026) on m1n1_windows and Mu, so it is not stalled. However `apple_silicon_nt_drivers` contains ONLY a README (a single 1519-byte file, 3 commits of 2025-08-28) and says "Nothing's implemented yet" [verified: `.../git/trees/HEAD?recursive=1`, `.../readme`]. So no code of the AIC HAL extension is public. The project itself describes itself as a "hobby project maintained by a few people" [verified: docs/project-info/when-will-ntasp-be-done.md].

Predecessor: `amarioguy/AppleWOAProject` is archived ("Deprecated NTASP project website"), contains only README/index/_config, last push 2026-05-06; no code [verified]. The same amarioguy has a 2022 Linux fork, irrelevant.

## 2. Effective boot architecture

- Hypervisor: YES, in the working path. `m1n1_windows` is described as m1n1 "with a GIC emulator" and contains `src/hv_vgic.c`, `hv_psci.c`, `hv_aic.c`, `hv_vm.c`, `hv_smmu.c`, `hv_vtpm.c`, `hv_vuart.c`, `hv_virtio.c` [verified: m1n1_windows tree]. The header of `hv_vgic.c` (commit 300b613a, 2026-07-20) says that the M1 implements the GIC CPU interface in hardware and m1n1 only needs to emulate the distributor, redistributor and possibly the ITS (8 list registers, 32 priority levels, 16 ID bits) [verified].
- The Mu README says: "can be built with support for the platform's AIC if you intend to run this without a vGIC (the default), or ... assuming a vGIC" [verified]. The README of the AIC driver (`Silicon/Apple/AppleSiliconPkg/Drivers/AppleAicDxe/Readme.md`) advises: "highly advised you run this EFI under a thin hypervisor providing a vGIC while the AIC support is experimental" [verified]. The DSC of T810XFamilyPkg, with AIC_BUILD==FALSE, places the GIC at 0xf00000000 / 0xf10000000 "virtualized by trusted firmware", and says that T8103 has no GIC [verified: `Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc`].
- The static MADT for M1 (`T810XFamilyPkg/AcpiTables/MADT_Static.aslc`, commit 19df1750, 2025-10-27) describes GICC per core, distributor, redistributor and ITS: i.e. Windows sees a virtual GICv3 [verified]. Windows therefore runs as an EL1 guest under the m1n1 hypervisor [strong hypothesis: consistent with all the elements above, but there is no document stating it in a single line].
- arm-trusted-firmware at EL2/EL3: the fork `arm-trusted-firmware-apple` exists, but it is stalled at 2023 and its commits are only the merge of upstream Arm [verified]. `hv_psci.c` states: PSCI for guests under the hypervisor, "a straight port of the implementation in ARM Trusted Firmware-A"; for bare metal "we use a higher level firmware running in GL2 to provide PSCI, so this file does not account for that case" [verified: src/hv_psci.c, commit 1e5adc1e, 2025-11-16]. The bare-metal case with PSCI from GL2 is therefore an intention, not existing code [hypothesis].
- Secondary cores: in the hypervisor path, via emulated PSCI (CPU_ON) in `hv_psci.c`, with power domain trees for T8103/T8112/T600x, plus `src/smp.c` and a `staging/smp` branch [verified in file names and source; the actual use of CPU_ON by Windows is not verified]. I found no parking protocol: searches for "MpPark" and "ParkingProtocol" in the org returned no results, and no file with "park" in the Mu tree [verified by absence]. The site says SMP is "still a tad bit fragile" [verified].
- Note the difference from our PLAN.md (parking protocol, no hypervisor, masked FIQs): NTASP takes the opposite road (vGIC + PSCI).

## 3. AIC and HAL

- Windows side: the HAL extension for the AIC is NOT public. The driver repo is empty and the site says "AIC: UEFI driver works, Windows HAL Extension needs more work" (m1.md) [verified]. So I cannot cite files or functions of the NT drivers: they do not exist in the repo. Registration mechanism in the HAL: not documented [hypothesis: HAL extension via CSRT, because CSRT.aslc says that ACPI is needed to signal to the OS the non-standard "interrupt controller and IOMMU"].
- What exists (UEFI side): `Silicon/Apple/AppleSiliconPkg/Drivers/AppleAicDxe/` with `AicV1/AppleAicV1Dxe.c`, `AicV2/AppleAicV2Dxe.c`, `AppleAicCommonDxe.c`, `AppleAicLib.c`, `AppleAicV1.S`, `AppleAicV2.S`. Declared status: "AICv2 driver finished, AICv1 in progress" (so for the M1, which is AICv1, it is still in progress) [verified]. The site instead says "UEFI driver works" for M1: slight inconsistency between sources.
- FIQ/IPI: the Readme says that the timers are wired as FIQ and that on AICv1 (M1) there are both "fast" FIQ IPIs and legacy IRQ IPIs; "if an AICv1 is detected, it will defer to using legacy IPIs" [verified]. This applies to the UEFI driver. For Windows: how they handle FIQs is not documented [hypothesis: under the vGIC the hypervisor translates].
- The timer: in Mu there are `AppleArmGenericTimerPhyCounterLib` and `...VirtCounterLib` [verified].
- One point in common with our plan: legacy IPIs via IRQ on M1. Difference: we want a native AIC in the HAL, they use a virtual GIC.

## 4. How far Windows boot gets

- Mu README: from external USB-C the UEFI starts ("not without hacks"), the Windows loader initializes and creates the RAMDisk, but "hangs on the handoff between bootloader and kernel" [verified: README of apple_silicon_platforms_mu; undated, but the repo was updated on 2026-09-08].
- Site: "we have Windows barely booting with some fundamentals working", working on ANS and AIC "as of 1/1/2026", the system will "stay alive for a portion of time (albeit very slowly ...) only boots tested were Windows to Go boots"; a second computer is needed [verified: when-will-ntasp-be-done.md]. Home: "currently not installable" [verified].
- The two statements are in tension (hang at the loader-to-kernel handoff vs. kernel that stays alive): the Mu README may be out of date [hypothesis]. I found no logs, videos or screenshots in the repos or on the site (the site only has the logo) [verified by absence in the pages read; Discord not consultable].
- "Windows to Go" implies an image on USB, not an installer [hypothesis].

## 5. M1 feature support (source: docs/feature-support/m1.md, commit 76abd81d; https://nt-for-asi.github.io/feature-support/m1/) [verified]

T8103 SoC blocks (valid for all M1 devices):

| Feature | Status |
|---|---|
| CPU core (Firestorm/Icestorm) | Partially working (SMP "a tad bit fragile") |
| DeviceTree | Mostly working |
| AIC | UEFI driver works, Windows HAL Extension needs more work |
| NVMe | In progress |
| Thunderbolt 3 | Not working (pending Type-C bringup) |
| DP Alt Mode | Not working (pending Type-C bringup) |
| AGX GPU | Not working |
| Display Controller | Partially working (iBoot framebuffer) |
| SEP | Not needed for now, optional |
| SMC | Not working |
| PCIe | Not working (pending DART bringup) |
| SPI | Not working |
| Wi-Fi + Bluetooth | Not working (depends on PCIe) |

For T600x the table is identical, except CPU "multi-die untested".

MacBook Pro (M1, 2020) J293AP: Installer Not working; USB-C Partially working (input only and partial storage); Battery Untested; Built-in display Working; Keyboard and trackpad Not working (pending SPI); Touch Bar Not working.
Mac mini (M1, 2020) J274AP: Installer Not working; USB-A Partially working (input only and partial storage); Ethernet Not working (no drivers); HDMI Out iBoot framebuffer only.
(The MacBook Air J313AP has the same rows as the Pro without the Touch Bar.)
Note: "Built-in display: Working" is consistent with the iBoot framebuffer; it is not acceleration.

## 6. Licenses

- m1n1_windows: MIT (LICENSE file, and `3rdparty_licenses`). Own sources with SPDX `MIT OR BSD-3-Clause` (hv_psci.c) and `BSD-2-Clause-Patent OR MIT` (hv_vgic.c) [verified]. Obligations: keep copyright and license text; the headers cite the KVM vGIC code (a Linux derivative, GPL-2.0: to be verified file by file) [hypothesis].
- apple_silicon_platforms_mu: BSD-2-Clause per GitHub; README: "dual licensed under either the BSD 2 clause license or the MIT license", with components "(MIT OR BSD-2-clause) AND GPL" for code derived from Asahi, hence GPL on those files. The Mu submodules follow their own license (BSD-2-Clause-Patent) [verified].
- apple_silicon_nt_drivers: no license file. README: MIT by default, GPL2 for drivers derived from Linux [verified]. Without a LICENSE file, the legal default is the license declared only in the README: ask for confirmation [hypothesis].
- arm-trusted-firmware-apple: BSD-3-Clause of TF-A (file `license.rst`) [verified].
- Forking: possible for the MIT/BSD parts (obligation: keep notices). For the GPL parts, obligation to release modifications under GPL. Our CLAUDE.md already provides for provenance headers. Contributing: the Mu README says "Contributions are always welcome! ... pull request" [verified]. Windows: no Microsoft code in the repos read [hypothesis: not scanned in depth].

## 7. How to contribute

- Channels: Discord https://discord.gg/djXD4hXyYB and GitHub org https://github.com/NT-for-ASi (links from the site home) [verified]. Issues/PRs: Mu and m1n1_windows have 3 and 0 open issues [verified].
- Main maintainer: Arminder Singh (`amarioguy`), who signs almost all recent commits [verified]. The site is maintained by Simizfo, gus33000 (Gustave Monce, WOA Project), Orangera1n and others.
- NTASP declares itself NOT affiliated with Asahi Linux [verified: faq.md]. In issue AsahiLinux/m1n1#63 (opened by lennylxx on 2021-05-21, closed) marcan replied that Windows will never run on bare metal without Microsoft's cooperation, because it requires kernel changes, but it could in a "thin VM" that emulates an ARM SystemReady system [verified]. That is exactly the architecture chosen by NTASP.

## Assessment for Watari Boot

- No element shows that NTASP boots Windows without a hypervisor. [verified as far as public]
- Reusable parts: ACPI/CSRT/MADT for T8103 and the j293 tables (`Platform/MacBookProLate2020Pkg`), the UEFI AIC driver, DeviceTree parsing, `hv_psci.c` as a reference for the power domain tree.
- NOT available parts: AIC HAL extension (no code), kernel handoff, parking protocol.
- My limits: I did not read Discord, I did not open every Mu file in depth; the source lines cited are those seen via the API.
