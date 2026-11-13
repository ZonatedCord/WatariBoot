<!-- SPDX-License-Identifier: MIT -->
# State inherited from iBoot and m1n1 at the hand-over (T18 part a, issue #48 family)

Status: public-docs research only (no hardware, no downloads, no Microsoft binaries). Date 2026-11-13.
Every claim is marked **[verified: source]** or **[hypothesis]**. Sources were read as hardware facts only; no code is copied (m1n1 is MIT, Linux is GPL-2.0).
Marco's day-2 observation (`re/m3a-day2-results.md`, last section): the panel brightness set in macOS survives the reboot into m1n1 and the layer, although neither drives the backlight. That means firmware-initialised state survives the hand-over; this note lists what, and whether we can use it.

Source tags used below (all public):
- **m1n1** = `github.com/AsahiLinux/m1n1`, tag `v1.6.1` (the version on j293), as `src/<file>:<line>`.
- **docs** = `github.com/AsahiLinux/docs`, branch `main`, read 2026-11-13, as `docs/<path>:<line>`.
- **linux** = `github.com/torvalds/linux`, tag `v6.12`.
- **asahi-dts** = `github.com/AsahiLinux/linux`, branch `asahi` (a moving branch, read 2026-11-13; downstream device trees for hardware the mainline tree does not describe yet, e.g. DCP, SPI keyboard, USB).

## 0. Summary table

"Left by" says who last touched the block before our layer runs. "Reuse" is for the layer (L), EDK2 (E), Windows (W).

| # | Item | State at hand-over | Left by | Reuse | Risk now |
|---|---|---|---|---|---|
| 1 | Panel scanout (DISP0 pipe, framebuffer 0x9e2570000, 2560x1600, x2r10g10b10) | **Running**, dart-disp0 locked, PMGR `disp0_fe`/`disp0_cpu0` on | iBoot; m1n1 may re-run the modeset (section 2) | L yes (already used, day 2), E/W yes as a GOP/simple framebuffer | Low if nobody touches DISP0/DCP PMGR or the DARTs; high if someone does (blank panel, no recovery without reboot) |
| 2 | DCP coprocessor (RTKit) | **Alive but quiesced** if m1n1 started it, otherwise as iBoot left it (**unknown**) | m1n1 `display_shutdown` / iBoot | No driver exists (Linux DCP is downstream, large); do not reuse | Medium: a live coprocessor with a DART window onto RAM above the guest's top |
| 3 | Backlight level | Level kept (Marco, day 2). Who holds it is **[hypothesis]**: SMC or the panel/backlight driver (docs say SMC handles the LCD backlight) or NVRAM `backlight-nits` applied by iBoot | iBoot / SMC | Nothing to do; do not touch. Changing it later needs SMC or DCP | Low; but brightness cannot be changed from our code |
| 4 | ANS2 (NVMe coprocessor, RTKit) | **Unknown**. m1n1 never starts ANS in proxy mode; iBoot2 "puts some coprocessors to sleep" | iBoot | W/E cannot use as is (needs RTKit boot, see `re/m3b-ans-windows.md`) | **High until measured**: if RTKit is still running, its SART-allowed buffers may sit in RAM that we hand to the guest |
| 5 | SART (ANS DMA filter) | Entries as iBoot left them; **unknown** | iBoot | Must be read before ANS is touched | High until measured (same as 4) |
| 6 | Internal keyboard + trackpad (SPI3 HID transport, `spien` GPIO 195) | iBoot used it for key combos; state afterwards **unknown** | iBoot | Possibly: no coprocessor firmware on the M1 (MTP is M2 and later) | Low (no DMA masters involved on SPI); the main gap is a driver |
| 7 | Touch Bar (j293 only): SPI0 multi-touch + DFR display pipe | Unknown; m1n1 only copies a calibration blob | iBoot | No | Low |
| 8 | USB-C: ATC PHY 0/1, DWC3 0/1, dart-usb, CD3217 PD controllers | PHY and DWC3 **powered**, DWC3 in **device mode, halted and soft-reset**; PD IRQ masks **disabled** and not restored on the kboot path | m1n1 | Powered blocks yes (hypothesis); host mode needs a role switch | Low-medium: no running DMA, but xHCI-host use needs PHY and PD work |
| 9 | PMGR power domains | Everything iBoot needed is on; m1n1 enabled parents of active domains and all domains it used (USB, PCIe, ANS2, DCP, SIO...) | iBoot, m1n1 | Read-only check by ehw2; do not power-gate anything | Medium: unlisted domains left on by m1n1 may keep coprocessors running |
| 10 | Clocks | MCA muxes set by m1n1; PMGR clock configuration as iBoot left it | iBoot, m1n1 | Yes (nothing to do) | Low |
| 11 | NVRAM (SPI NOR partition `anvram` at 0x700000) | Variables are in flash; **iBoot applies them** (hypothesis for which ones). m1n1 never reads or writes them in the kboot path | iBoot | Readable from macOS (`nvram -p`); never write | Low; writing could brick boot (project rule: NEVER write NVRAM) |
| 12 | Watchdog | **Disabled** by m1n1 | m1n1 | The layer re-arms it for reset (works, day 2) | Low |
| 13 | SEP | Put to sleep by iBoot; PMGR `sep` locked on | iBoot | Not needed | None (isolated by design) |
| 14 | SMC (RTKit) | Presumably running (power button, lid, battery) | iBoot | Later, for power-off | Low |
| 15 | PCIe (Wi-Fi, BT, ports) | **m1n1 brings the root complex and PHYs up** in `kboot_boot` (`pcie_init`); DART state unknown | m1n1 | W could see devices; DMA needs the DART answer | **High** for DMA until the apcie DARTs are read |
| 16 | Memory controller / cache | m1n1 `mcc_enable_cache`, tunables applied | m1n1 | Nothing to do | Low |

## 1. How the hand-over actually works

### 1.1 Chain of custody
- SecureROM validates iBoot1; iBoot1 reads system-global firmware, **bootstraps some coprocessors**, shows the logo and picks a boot policy; iBoot2 reads OS-paired firmware, **loads and locks some of it, re-bootstraps the display coprocessor**, reads the ADT and the kernel, **puts some coprocessors to sleep**, and jumps to the "kernel" (here m1n1) [verified: docs/platform/introduction.md:112-127].
- "At no point in this process is any I/O initialized other than the screen, the speakers, and checking for power button presses or (on laptops) specific keys" [verified: docs/platform/introduction.md:131]. The majority of firmware blobs are already loaded by the time a third-party OS boots [verified: docs/platform/open-os-interop.md:133].
- The SEP is put to sleep before the OS kernel boots; the OS would have to reload its firmware [verified: docs/platform/introduction.md:151].
- Our setup: Asahi stub volume "Tethered boot", m1n1 v1.6.1 in proxy mode, `watari-load.py` writes the layer into RAM and calls the proxy request `kboot_boot` [verified: `re/m3a-ehw0-measurements.md` header; `tools/m3a/watari-load.py:17`].

### 1.2 What m1n1 v1.6.1 does before our layer runs
Start-up (`m1n1_main`, src/main.c:142-188): `mcc_init`, `mmu_init`, `aic_init`, `wdt_disable`, `pmgr_init`, `display_init`, `display_shutdown(DCP_SLEEP_IF_EXTERNAL)`, `fb_init`, `cpufreq_fixup`, `sep_init` [verified: src/main.c:150-183]. Notes:
- `wdt_disable` writes `WDT_CTL = 0` on the primary watchdog (and zero to the second one on newer WDT versions) [verified: src/wdt.c:14-46]. Matches day 1: "watchdogs ... disabled by m1n1".
- `pmgr_init` reads every non-virtual PMGR device, and for each device that is auto-enabled or has target state ACTIVE it **powers the parents** (`PS_ACTIVE`, 0xf) [verified: src/pmgr.c:423-467]. Matches day 1: "m1n1 enabled the PMGR parents of ANS2, SIO_ADMA, DEBUG on its own".
- `sep_init` only maps the SEP ASC (`asc_init`); it starts nothing [verified: src/sep.c:23-30].

The proxy request `kboot_boot` (src/proxy.c:406-409 calls `kboot_boot`, then the main loop falls through to the tail) runs `mcc_enable_cache`, `tunables_apply_static`, `clk_init`, `usb_init`, `pcie_init`, `dapf_init_all`, sets SMP to WFE mode and records the next stage [verified: src/kboot.c:2862-2885]. Then the tail of `m1n1_main` runs: `nvme_shutdown`, `exception_shutdown`, `usb_iodev_shutdown`, `display_shutdown(DCP_SLEEP_IF_EXTERNAL)`, `fb_shutdown(next_stage.restore_logo)` (false for kboot), `mmu_shutdown`, then jumps [verified: src/main.c:190-210, src/kboot.c:2882].
- `clk_init` only sets the MCA (audio) clock muxes [verified: src/clk.c:14-41].
- `dapf_init_all` programs the DAPF of dart-aop, dart-mtp, dart-pmp, dart-isp, dart-isp0, nothing else [verified: src/dapf.c:173-176].
- `kboot_prepare_dt` (run before `kboot_boot`) also **locks dart-disp0** (`dart_lock_adt("/arm-io/dart-disp0", 0)`) and sets up display reserved-memory regions so later software does not reset it [verified: src/kboot.c:1935-1938].

## 2. Display, DCP and backlight

### 2.1 What is running
- Panel scanout: the framebuffer base, 2560x1600, 30 bpp at 0x9e2570000 [verified: `re/m3a-ehw0-measurements.md` "Also seen in the m1n1 boot log"; day 2 L0]. It stays on screen after `fb_shutdown(false)`; the layer draws on it (Q-3, Q-5, day 2).
- On a j293, `display_init` picks one of three paths: reconfigure when the boot args do not carry the retina flag ("Sonoma bug"); **power-cycle and reconfigure when the system firmware is 15.0b1 or newer and the stub OS firmware is older** (not compiled in for a chainloaded build); otherwise "already initialized" [verified: src/display.c:642-665]. On j293 the system firmware is mBoot-18000 (macOS 26.5.1) and the stub OS firmware is 13.5, so the power-cycle path is **probable** [hypothesis: depends on whether the installed stage-1 m1n1 was built with `CHAINLOADING`; the m1n1 console lines "display: ..." would settle it, they were not kept].
- If m1n1 did start DCP (`iboot != NULL`), `display_shutdown` calls `dcp_ib_shutdown` and then `dcp_shutdown(dcp, false)` for an internal panel: the RTKit is moved to the **QUIESCED** power state (not SLEEP, no PMGR reset) [verified: src/display.c:668-695, src/dcp.c:252-266, src/rtkit.c:659-715 (AP and IOP go to 0x10)]. The `display: ...` modeset ends with a swap that points the pipe at the framebuffer and waits 150 ms before powering the coprocessor down [verified: src/display.c:562-572]: the scanout keeps running without DCP.
- If m1n1 did not start DCP (the "already initialized" path), `iboot` stays NULL, `display_shutdown` does nothing, and the DCP coprocessor remains as iBoot2 left it ("re-bootstraps display coprocessor", docs/platform/introduction.md:123) [verified: src/display.c:674-694 guard]. State then **unknown** until ehw2 reads `CPU_CONTROL`.
- Linux treats the two PMGR domains that feed the display (`disp0_fe`, `disp0_cpu0`) as **always-on** ("TODO: figure out if we can enable PM here") [verified: linux t8103-pmgr.dtsi, `ps_disp0_fe@360` and `ps_disp0_cpu0@10018`]. DCP is at 0x231c00000, its mailbox at 0x231c08000, dart-disp0 at 0x231304000 and dart-dcp at 0x23130c000 on the t8103 [verified: asahi-dts t8103.dtsi:610-652].

### 2.2 Backlight
- The brightness-level provenance is not documented publicly. Candidate holders:
  1. **SMC**: "the SMC ... handles ... the LCD backlight and lid switch" [verified: docs/hw/soc/smc.md:5]. The SMC keeps running across the hand-over (nobody stops it) [hypothesis].
  2. **DCP/panel**: Linux sets the backlight through DCP as a DAC value (`dcp_backlight.c` converts nits to a DAC code and sends it over the IOMFB endpoint; DCP reports the initial nits at probe) [verified: asahi-dts `drivers/gpu/drm/apple/dcp_backlight.c:85-218`, `iomfb_template.c:204-214`]. A quiesced DCP would not clear the panel's own DAC register [hypothesis].
  3. **NVRAM `backlight-nits`**: documented as "presumably the screen backlight strength" (Mac mini M1 example `0x008c0000`) [verified: docs/fw/nvram.md:49]; whether iBoot applies it at boot is **[hypothesis]**.
- j293 panel: `apple,panel-j293`, `apple,max-brightness = <525>` [verified: asahi-dts t8103-j293.dts:43-50].
- The keyboard backlight is a separate PWM (`fpwm1`, 0x235044000, clock gate at PMGR 0x23b7001e0); the mainline j293 tree sets its LED `default-state = "keep"`, i.e. Linux itself relies on inherited firmware state [verified: docs/hw/peripherals/keyboard-backlight.md:21; linux t8103-j293.dts:20-30].

### 2.3 Reuse and risk
- Layer: keep using the scanout as a framebuffer (done). EDK2/Windows: GOP / `simple-framebuffer` (design `docs/design/watari-edk2-m1/DESIGN.md` section 8). Nothing to initialise.
- The display keeps working only while nobody resets DISP0/DCP PMGR domains, resets dart-disp0 / dart-dcp, or writes the display DART TTBRs. Neither Windows' ACPI nor EDK2 may expose or power-manage those blocks. There is no way back to a working display short of a reboot [hypothesis: nothing in the public docs describes a restart of DCP without its RTKit endpoints].
- The framebuffer lies **above** the top of normal RAM (0x9e2570000 vs 0x9df610000) [verified: ehw0 "Also seen in the m1n1 boot log"], so a DCP that wakes up and DMAs through dart-disp0 reads memory the guest does not own.

## 3. ANS2, SART and DART (storage)

### 3.1 What iBoot and m1n1 do
- iBoot reads iBoot2 and the kernel from the internal NVMe, so ANS2 was booted early in the chain; iBoot2 then "puts some coprocessors to sleep" [verified: docs/platform/introduction.md:120-126]. Whether ANS is one of them is **[hypothesis]**.
- m1n1 initialises ANS only when asked (`nvme_init` is called by the chainload code and by the proxy request `P_NVME_INIT`); on a plain proxy session nothing calls it [verified: gh code search of the repository: callers are `src/chainload.c`, `src/proxy.c`]. If it had been called, `nvme_shutdown` at the tail of `m1n1_main` deletes the queues, shuts the controller down, sends RTKit to SLEEP and **resets the PMGR devices "ANS" and "ANS2"** [verified: src/nvme.c:479-516]. Our Asahi stub is "Tethered boot" with no ESP chainload, so this path does not run [verified: `re/m3a-ehw0-measurements.md` header, Asahi OS "Tethered boot"].
- m1n1 contains a helper `nvme_ensure_shutdown` for the case "Found ANS left powered" (checks `CPU_CONTROL` bit 4 of the ASC, boots RTKit, sleeps it, resets the PMGR devices) but has **no live call** in v1.6.1 (the call in `nvme_shutdown` is commented out) [verified: src/nvme.c:434-477, 482; code search found only the definition]. This suggests the m1n1 authors saw ANS left powered in some flow; which flow is **[hypothesis]**.
- Day 1 measured the PMGR side: m1n1 itself enabled the PMGR parents of ANS2 (they were off or in a gated state before) [verified: `re/m3a-ehw0-measurements.md` "Also seen in the m1n1 boot log"]. So ANS2 or something below it was not left fully on by iBoot [hypothesis: reading the `ans2` PS register at 0x23b7003f0 tells].

### 3.2 Where the state is
- ASC (coprocessor) block: 0x277400000 (`/arm-io/ans` reg 0); CPU control at +0x44, START = bit 4; status at +0x48 (bit 0 IDLE, bit 1 STOPPED, bits 2-3 IRQ/FIQ not pending, bit 5 mailbox to AP empty); mailbox at +0x8000 (queue status registers +0x8110 CPU-to-IOP, +0x8114 IOP-to-CPU, bit 16 not empty, bit 17 empty) [verified: src/asc.c:8-24, 52-53, 79-82; docs/hw/soc/asc.md:8-20, 60-66; linux t8103.dtsi:686-698 mailbox at 0x277408000]. Linux also starts/stops ANS by writing the same `CPU_CONTROL` (0x44, RUN = bit 4) [verified: linux drivers/nvme/host/apple.c:40-41, 1016, 1030-1031].
- NVMe controller block: 0x27bcc0000, 0x40000 long; the **boot status word at +0x1300** reads `0xde71ce55` when the firmware is up [verified: linux t8103.dtsi:706-710; apple.c:48-49, 1038 (read through the `nvme` mapping); src/nvme.c:39-40, 350]. Correction to `re/m3b-ans-windows.md` section 1.2, which said "boot status register at `ans+0x1300`": the poll in Linux and m1n1 reads the **nvme** region, not the coprocessor region.
- SART: 0x27bc50000, 0x10000 long; on t8103 SART version 2: 16 entries, `CONFIG(i)` at +4*i (flags bits 31:24, size bits 23:0, 4 KB units), `PADDR(i)` at +0x40+4*i (4 KB units); allow flags `0xff` [verified: linux t8103.dtsi:700-704; src/sart.c:19, 34-43, 154-208 (version picked by the ADT `sart-version`); version 2 for t8103 is from `re/m3b-ans-windows.md` 1.4, itself verified there].
- **No DART in ANS's data path**: m1n1's NVMe driver uses SART only (the file contains no `dart` or `iommu` reference), and the t8103 `nvme@27bcc0000` node has no `iommus` property [verified: src/nvme.c (grep, no matches); linux t8103.dtsi:706-718]. This answers the open question in `re/m3b-ans-windows.md` 1.5 for the t8103: ANS DMA is SART-filtered with physical addresses, not translated. Mailbox IRQs 583-586 and level IRQ 590 (measured day 1).

### 3.3 Reuse and risk
- Reuse: only if ANS is **cleanly sleeping** (RTKit SLEEP or CPU stopped); then a Windows miniport or an EL2 helper does the full RTKit boot (see `re/m3b-ans-windows.md` section 5). If RTKit is **running**, nobody can boot it again without shutting it down first (a second HELLO needs the AP to speak the protocol from the start; Linux refuses a reset after a crash) [hypothesis for "second HELLO"; verified for the crash rule in apple.c:997-1003 per `re/m3b-ans-windows.md` 1.2].
- Risk: a running ANS RTKit owns buffers (syslog, crashlog, command memory) that iBoot placed somewhere and listed in SART. Whether those buffers lie in the 0x802078000-0x9df45c000 RAM we give to the guest, or in firmware carve-outs, is **unknown**. If they are in guest RAM and ANS writes a log line while Windows owns the page, memory is corrupted silently. Linux itself treats entries the bootloader left in SART as protected for this reason [verified: sart.c probe, per `re/m3b-ans-windows.md` 1.4].
- Decisive measurement: `CPU_CONTROL` bit 4, boot status word, SART entries, and the PMGR `ans2` state (section 5).

## 4. Other items

### 4.1 Keyboard and trackpad
- j293 (and the other M1 MacBooks) use an **SPI HID transport on spi3** (0x23510c000, AIC IRQ 617) with a dedicated enable GPIO (`spien`, pinctrl_ap 195) and a wake line on pinctrl_nub 13; m1n1 reads the ADT node `/arm-io/spi3/ipd` for the keyboard layout code, and falls back to `/arm-io/dockchannel-mtp/mtp-transport/keyboard` (MTP, M2 and later) [verified: asahi-dts t8103.dtsi:862-870, t8103-j293.dts:77-96; src/kboot.c:898-940].
- There is no coprocessor firmware to load for the M1 keyboard: the packaged vendor blobs list MTP firmware for **M2** machines only [verified: docs/platform/open-os-interop.md:139-145]. Neither the SPI controller driver nor the HID transport is in Linux v6.12 (`drivers/spi` has no Apple file; the t8103 `spi3` node exists only in the asahi tree), and the support matrix lists Keyboard and Touchpad as "linux-asahi" only [verified: gh contents listing of `drivers/spi` at v6.12; asahi-dts j293 dts above; docs/platform/feature-support/m1.md rows "Keyboard", "Touchpad"].
- State at hand-over: iBoot polls "specific keys" on laptops [verified: docs/platform/introduction.md:131], so SPI3 and the keyboard controller were initialised at some point; whether they are still configured is **unknown**. PMGR domains involved: `sio`, `sio_cpu`, `spi_p`, `spi3` (offsets 0x1c8, 0x1d0, 0x218, 0x258 in the main PMGR) [verified: linux t8103-pmgr.dtsi].
- Touch Bar (j293): `/arm-io/spi0/multi-touch` (calibration blob copied by m1n1) and the DFR display pipe (`dispdfr_fe`, `dispdfr_be`) [verified: src/kboot.c:863-893; asahi-dts display_dfr@228200000 lines ~551-600].
- Reuse: an EDK2 keyboard driver could use SPI3 plus the HID framing, but needs the reverse-engineered protocol (downstream code) and the SPI controller, so it is real work, not a hand-over freebie. Windows: no inbox SPI HID transport for this controller [hypothesis]. Risk: none of these are DMA masters of RAM beyond their own FIFOs [hypothesis: SPI controller uses SIO DMA only if configured, see `sio_dart` at 0x235004000].

### 4.2 USB-C: ATC PHY, DWC3, PD controllers
- m1n1 `usb_init` (in `kboot_boot`, and at start for the proxy) enables the PMGR domains of `atc-phy<N>`, `dart-usb<N>`, `usb-drd<N>`, writes the PHY registers (`atc+0x08/0x04/0x1c/0x00`) and the pipehandler (`MUX_CTRL = DUMMY 0x22`, `AON_GEN = DWC3_RESET_N`, `NONSELECTED_OVERRIDE = 0x9332`): USB2 through a "dummy" USB3 path only [verified: src/usb.c:126-160, 318-351].
- The DWC3 is initialised in **device mode** (`GCTL.PRTCAPDIR = DEVICE`) for the proxy console; `usb_iodev_shutdown` stops all transfers, clears RUN_STOP, waits for `DEVCTRLHLT` and does a controller soft reset; it does **not** power anything down or switch to host mode [verified: src/usb_dwc3.c:1186-1187, 1269-1296; src/usb.c:450-461].
- The CD3217 USB-PD controllers (`hpm0` at I2C 0x38, `hpm1` at 0x3f on i2c0) get their interrupt masks **disabled** in `usb_init_i2c` and restored only by `usb_hpm_restore_irqs`, which the proxy or the hypervisor must call; the kboot path does not [verified: src/usb.c:265-313, 353-424; src/proxy.c (callers); linux t8103-jxxx.dtsi:49-65; controller is "CD3217 ACE2", docs/hw/soc/usb-pd.md:9, 24].
- DWC3 DMA goes through `dart-usb<N>` (ADT, `FMT_DART_PATH`), which m1n1 initialises with its own mappings [verified: src/usb.c:36-39, 164-172]. After `DEVCTRLHLT` plus soft reset the controller runs no transfers, but the DART still holds m1n1's translation entries [hypothesis].
- Reuse: PHY and DWC3 are powered and clocked; to be an xHCI host for Windows the role must be switched, USB3 routed through the PHY (the Linux `atcphy` driver) and the PD controller told to select the data role. Public Linux status: "USB2/USB3 (TB ports)" arrives in 7.0 [verified: docs/platform/feature-support/m1.md:28-29]. So E/W reuse is **partial**, and DWC3 host mode is a separate research item (T15).
- Risk: low. A stale DART entry could only matter if the controller were started.

### 4.3 PMGR and clocks
- Power state register layout (identical in m1n1, Linux and the Asahi Python client): target bits 3:0, actual bits 7:4, 0xf = active, 0x4 = clock-gated, 0x0 = power-gated; bit 8 `WAS_PWRGATED`, bit 9 `WAS_CLKGATED`, bit 10 `DEV_DISABLE`, bit 11 `PARENT_OFF`/missing, bit 28 `AUTO_ENABLE`, bit 31 `RESET` [verified: src/pmgr.c:9-17; linux drivers/pmdomain/apple/pmgr-pwrstate.c:20-35; m1n1 proxyclient `hw/pmgr.py:4-13`].
- Register blocks on t8103: main PMGR at 0x23b700000 (0x14000 long), "mini" PMGR (always-on domains, USB debug, nub*) at 0x23d280000, plus a DCP-bandwidth block at 0x23b738000 [verified: linux t8103.dtsi:547-552, 629-642; asahi-dts t8103.dtsi:1054]. The proxy client's own map: PMGR ADT reg 0 holds device state registers (ranges starting 0x0, 0x100, 0x200, 0x300, 0x400, 0xc00, 0x4000, 0x8000, 0xc000, 0x10000), reg 1 the mini PMGR (0x58, 0x4000, 0x8000), reg 2 the clock configuration registers at 0x40000 (86), 0x40200 (8), 0x40280 (2) [verified: m1n1 proxyclient `hw/pmgr.py:25-55`].
- Domains Linux marks `apple,always-on` ("core device", "locked on"): sbr, aic, dwi, pms_busif, pms, soc_dpe, imx, mcc, dcs0-7, smx, disp0_fe, sep, disp0_cpu0 and in the mini block debug, nub_spmi0/1, nub_aon, nub_gpio, nub_fabric, nub_sram, debug_usb, debug_auth [verified: linux t8103-pmgr.dtsi, `apple,always-on` entries]. Everything else is on only because iBoot or m1n1 needed it.
- Relevant offsets in the main PMGR (all [verified: linux t8103-pmgr.dtsi]): `ans2` 0x3f0, `apcie_st` 0x418, `apcie` 0x348, `apcie_gp` 0x3e8, `disp0_fe` 0x360, `disp0_cpu0` 0x10018, `dispext_fe` 0x368, `dispext_cpu0` 0x378, `dispdfr_fe` 0x3a8, `dispdfr_be` 0x3b0, `sep` 0xc00, `sio_busif` 0x1c0, `sio` 0x1c8, `sio_cpu` 0x1d0, `fpwm1` 0x1e0, `i2c0` 0x1f0, `spi_p` 0x218, `spi0` 0x240, `spi1` 0x248, `spi3` 0x258, `spi4` 0x260, `uart0` 0x270, `gpio` 0x130, `pmp` 0x3d8, `mcc` 0x2f8, `atc0_common` 0x420, `atc0_pcie` 0x428, `atc0_cio` 0x430, `atc0_cio_pcie` 0x438, `atc0_cio_usb` 0x440, `atc1_common` 0x448, `atc1_pcie` 0x450, `atc1_cio` 0x458, `atc1_cio_pcie` 0x460, `atc1_cio_usb` 0x468. In the mini PMGR: `atc0_usb_aon` 0x88, `atc1_usb_aon` 0x90, `atc0_usb` 0x98, `atc1_usb` 0xa0, `debug_usb` 0xb8.
- Reuse: Windows must not power-gate any of these; our ACPI exposes only CPUs, GIC, timer, and the UART at first (design). Risk: a domain left on that hosts a running coprocessor (ANS2, DCP, SMC, AOP, PMP) keeps running; this is the point of the ehw2 reads.

### 4.4 NVRAM
- The NVRAM lives in the SPI NOR (`/arm-io/spi1/spinor/anvram`), partition start **0x700000**, at most 1 MiB; m1n1 only copies the partition's `reg` into the FDT `nvram` node for Linux's MTD/NVMEM driver; it never reads the variables [verified: src/kboot.c:943-992].
- Known variables (from the community list): `boot-args`, `boot-volume`, `auto-boot`, `backlight-nits`, `SystemAudioVolume`, `prev-lang:kbd`, `good-samaritan-message`, `preferred-networks`, Bluetooth controller info, `usbcfwflasherResult`, ... [verified: docs/fw/nvram.md:19-63]. Which ones iBoot **applies** to hardware (backlight, audio volume, keyboard layout) is **[hypothesis]**: the docs list the variables but do not say who consumes them.
- Linux does not set brightness from NVRAM; it asks DCP for the current nits at probe [verified: asahi-dts iomfb_template.c:204-214, see 2.2].
- Reading NVRAM without writes is possible from macOS (`nvram -p`, read-only) before the reboot into m1n1; from the proxy it would need SPI1 access to a flash chip, which is **not** a pure register read and is out of scope for ehw2. Never write NVRAM (project rule). Test of the "iBoot applies `backlight-nits`" hypothesis: in macOS change the brightness, print the variable with `nvram backlight-nits`, repeat; if it never changes with the slider, hypothesis rejected.

### 4.5 Other coprocessors and blocks left behind
- SMC (RTKit, 0x23e400000, mailbox 0x23e408000), SEP (0x242400000, PMGR `sep` "locked on"), AOP (0x24ac00000), PMP (PMGR `pmp`), DCP, ANS: all on-SoC coprocessors are behind IOMMU-like firewalls except the GPU firmware [verified: docs/platform/introduction.md:47, 67; addresses from asahi-dts t8103.dtsi:1235, 1309, 1396]. Their RTKit state at hand-over is unmeasured on j293 [hypothesis: SMC runs, SEP sleeps, AOP/PMP unknown].
- PCIe: `pcie_init` powers the controller, applies the ADT tunables, releases PHY reset and initialises each port's configuration; it does not set up the root-port DARTs (Linux does) [verified: src/pcie.c:424-481, 559-700, 842-856; DART bases 0x681008000, 0x682008000, 0x683008000 from linux t8103.dtsi:720-745]. The DART translation mode left for the Wi-Fi/BT/SD ports is **unknown**.
- Memory controller: `mcc_enable_cache` and static tunables are applied by m1n1; nothing for us to undo [verified: src/kboot.c:2864-2865].

## 5. Proposed read-only list for `tools/m3a/ehw2.py` (feeds part b)

General rules for the script (same as `ehw0.py`): never import `m1n1.setup`; only `read32`/`read64` on the proxy and ADT parsing on the host; **no writes of any kind**; `--dry-run` prints the plan with addresses and the gating decision without touching the proxy; a fake-proxy test asserts that no write method is ever called. Derive every address from the ADT where possible and compare with the constant in the table; on a mismatch print both and **skip** the register (never guess).

**Gating (mandatory):** reading the registers of an unpowered block can hang or raise an SError. For every block, first read its PMGR PS register and read the block only if `ACTUAL == 0xf`. **Exception guard (mandatory):** the `GUARD_RETURN` in src/uartproxy.c:225 covers only the bulk `readmem` request; `p.read32`/`p.read64` are `P_READ32`/`P_READ64`, and the exception guard defaults to `GUARD_OFF` (src/exception.c:21), so a faulting read ends in "Unhandled exception, rebooting..." (`flush_and_reboot`, src/exception.c:356-359; SError path near 470) [verified by the independent verifier, 2026-11-13]. ehw2 must therefore call `p.set_exc_guard(GUARD.SKIP | GUARD.SILENT)` (proxyclient proxy.py:479-484, 799) **before the first read**, check `p.get_exc_count()` (proxy.py:801) after **each** read and treat a change as "read faulted, value invalid", and restore `GUARD.OFF` at exit (also on error). A bus that never answers may still hang the proxy; the guard does not help there [hypothesis: whether the t8103 fabric hangs or faults on a gated block].

**Caveat: what ehw2 can and cannot see.** ehw2 runs over the proxy **before** `kboot_boot` and before the tail of `m1n1_main` (section 1.2). It therefore reads the state m1n1 has **while serving the proxy**, not the true hand-over state: the DWC3 of the console port reads as running device mode (RUN_STOP = 1, not halted); `pcie_init` has not run, so `apcie_gp` may still be off (then the apcie DARTs are skipped by the gate); `nvme_shutdown`, `display_shutdown`, `fb_shutdown` and `mmu_shutdown` have not run yet. The exact hand-over state needs a read from inside the layer (a later item).

### 5.1 PMGR (always readable)
| What | Address | Decode | Source |
|---|---|---|---|
| All device PS registers of the main PMGR | derived from ADT `/arm-io/pmgr` `ps-regs` + `devices` (m1n1's `pmgr_device_get_addr`), same as proxyclient `PMGR.dump_all()` | `ACTUAL` 7:4, `TARGET` 3:0, `PARENT_OFF` 11, `DEV_DISABLE` 10, `WAS_CLKGATED` 9, `WAS_PWRGATED` 8, `AUTO_ENABLE` 28, `RESET` 31 | src/pmgr.c:9-17, 120-138; proxyclient hw/pmgr.py:57-71 |
| Named cross-check set (main PMGR, base 0x23b700000) | +0x3f0 ans2, +0x418 apcie_st, +0x348 apcie, +0x3e8 apcie_gp, +0x360 disp0_fe, +0x10018 disp0_cpu0, +0xc00 sep, +0x3d8 pmp, +0x1c8 sio, +0x258 spi3, +0x240 spi0, +0x1e0 fpwm1, +0x1f0 i2c0, +0x420..+0x468 atc0/atc1 common, pcie, cio, cio_pcie, cio_usb | as above | linux t8103-pmgr.dtsi |
| Mini PMGR (base 0x23d280000) | +0x88 atc0_usb_aon, +0x90 atc1_usb_aon, +0x98 atc0_usb, +0xa0 atc1_usb, +0xb8 debug_usb | as above | linux t8103-pmgr.dtsi; t8103.dtsi:629 |
| Clock configuration | **offsets relative to PMGR ADT reg 2** (never 0x23b700000 + 0x40000): 86 words at +0x40000, 8 at +0x40200, 2 at +0x40280 | raw words (`SRC` bits 30:24); record only | proxyclient hw/pmgr.py:18-23, 52-55 |

### 5.2 Coprocessors (read only if the owning PS is 0xf)
| Block | Base | Registers (read32) | Owning PS | Source |
|---|---|---|---|---|
| ANS ASC | 0x277400000 (ADT `/arm-io/ans` reg 0) | +0x44 CPU_CONTROL (bit 4 = running), +0x48 CPU_STATUS, +0x8110 and +0x8114 mailbox status (only bit 17 EMPTY is reliable: m1n1 `asc.c` names bit 16 FULL for the A2I side; "bit 16 = not empty" in the docs is [hypothesis]) | `ans2` 0x3f0 | src/asc.c:8-24; docs/hw/soc/asc.md:8-20, 66; linux t8103.dtsi:686-698 |
| ANS NVMe | 0x27bcc0000 (ADT `/arm-io/ans` reg 3 per src/nvme.c:310) | +0x1300 boot status (`0xde71ce55` = firmware up) and CSTS at the NVMe-standard offset +0x1c (Linux reads `NVME_REG_CSTS` through the same mapping) | `ans2` | linux apple.c:48-49, 801, 883, 1038; src/nvme.c:39-40 |
| SART (ANS) | 0x27bc50000 (ADT `/arm-io/sart-ans` reg 0) | 16 x `CONFIG` at +0x00+4*i, 16 x `PADDR` at +0x40+4*i (v2); print flags, size (x 4 KB), paddr (x 4 KB) and flag whether the range overlaps the guest RAM list of `kboot_prepare_dt` | `ans2` | src/sart.c:34-43; linux t8103.dtsi:700-704 |
| DCP ASC | 0x231c00000 (ADT `/arm-io/dcp` reg 0) | +0x44, +0x48, +0x8110, +0x8114 | `disp0_cpu0` 0x10018 | asahi-dts t8103.dtsi:644-652; src/asc.c |
| SMC ASC | 0x23e400000 (ADT `/arm-io/smc`) | +0x44, +0x48 | none known in Linux; read only if the ADT node gives a `clock-gates` entry, else **skip** | asahi-dts t8103.dtsi:1235 |
| SEP ASC | 0x242400000 (ADT `/arm-io/sep`) | +0x44, +0x48 (expect stopped or idle) | `sep` 0xc00 | asahi-dts t8103.dtsi:1309; docs/platform/introduction.md:151 |
| AOP, PMP ASC | ADT `/arm-io/aop`, `/arm-io/pmp` reg 0 (aop mailbox at 0x24a408000 implies ASC base 0x24a400000, to be checked against the ADT) | +0x44, +0x48 | none known in Linux for AOP; PMP: `pmp` 0x3d8 if the ADT agrees; read only if the ADT gives a gate, else **skip** | asahi-dts t8103.dtsi:1360, 1396 |

### 5.3 DARTs (read only if the owning PS is 0xf; no register is written, no stream command issued)
Per DART (`dart,t8020` style on the t8103): `PARAMS1` +0x00 (page shift bits 27:24), `PARAMS2` +0x04 (bypass support bit 0), `ERROR` +0x40, `CONFIG` +0x60 (lock = bit 15), `ENABLED_STREAMS` +0xfc, and for each stream 0..15 `TCR` at +0x100+4*s (translate bit 7, bypass-DART bit 8, bypass-DAPF bit 12) and the first `TTBR` at +0x200+16*s (valid bit 31, address bits 30:0 in 4 KB units) [verified: src/dart.c (T8020 defines); linux apple-dart.c:43-47, 69, 79-87]. Do **not** read anything else (no `STREAM_COMMAND`, `STREAM_SELECT`, TLB registers).
| DART | Base | Why | Source |
|---|---|---|---|
| dart-disp0, dart-dcp (gate `disp0_cpu0` 0x10018) | 0x231304000, 0x23130c000 | confirm the lock bit, the framebuffer mapping, no active error | asahi-dts t8103.dtsi:610-626; src/kboot.c:1938 |
| dart-usb0, dart-usb1 (gate `atc0_usb` 0x98 / `atc1_usb` 0xa0, mini PMGR; ADT `/arm-io/dart-usb<N>`; Linux names them `dwc3_N_dart_0/1` at 0x382f00000, 0x382f80000 and 0x502f00000, 0x502f80000) | as given | m1n1 left translation entries from its USB console | asahi-dts t8103.dtsi:1664-1679, 1813-1828; src/usb.c:36-39 |
| apcie DARTs 0..2 (gate `apcie_gp` 0x3e8) | 0x681008000, 0x682008000, 0x683008000 | bypass vs translate for Wi-Fi/BT/SD DMA | linux t8103.dtsi:720-745 |
| sio DART (gate `sio_cpu` 0x1d0) | 0x235004000 | SPI/I2C DMA masters | linux t8103.dtsi:359-366 (v6.12 `sio_dart`) |

### 5.4 USB and SPI blocks
| Block | Registers (read32) | Gate | Source |
|---|---|---|---|
| DWC3 0 and 1 (ADT `/arm-io/usb-drd<N>` reg 0; asahi-dts shows 0x382280000 and 0x502280000) | `GCTL` +0xc110 (bits `PRTCAPDIR`: host / device / OTG), `GSTS` +0xc118 (current mode, bits 1:0: 0 device, 1 host; linux drivers/usb/dwc3/core.h:294-296), `DCTL` +0xc704 (RUN_STOP bit 31), `DSTS` +0xc70c (`DEVCTRLHLT` bit 22) | `atc0_usb` 0x98 / `atc1_usb` 0xa0 (mini PMGR) | src/usb_dwc3_regs.h:91-138; src/usb.c:114; asahi-dts t8103.dtsi:1648, 1797 |
| ATC PHY 0 and 1 (ADT `/arm-io/atc-phy<N>` reg 0; 0x383000000 and 0x503000000) | the four words m1n1 writes: +0x00, +0x04, +0x08, +0x1c (record raw) | same | src/usb.c:149-153; asahi-dts t8103.dtsi:1682, 1831 |
| Pipehandler (ADT `/arm-io/usb-drd<N>` reg 3; cross-check 0x382a84000 for usb-drd0, per the verifier) | +0x0c MUX_CTRL, +0x1c AON_GEN, +0x20 NONSELECTED_OVERRIDE | same | src/usb.c:47-63, 118, 155-157 |
| SPI3 controller 0x23510c000 | none beyond the PMGR states of `sio`, `spi_p`, `spi3` (the controller's register map is not documented publicly; reading unknown registers is not justified) | PMGR only | asahi-dts t8103.dtsi:862 |
| GPIO 195 `spien` (pinctrl_ap 0x23c100000) | skip: the pin-control register layout is Linux-driver knowledge (`apple-pinctrl`), keep for a later read-only addition | n/a | asahi-dts j293 dts:93 |

### 5.5 NVRAM and ADT-only reads (no hardware access beyond the ADT in RAM)
- **NVRAM variables: not read from the proxy.** Instead, the day procedure gets a macOS-side step before the reboot: `nvram -p` (read-only), saved to the host folder (contents are private: not committed). The script may only **print the ADT `/options` node and `/chosen` properties** that the host already holds in RAM through the proxy `u.adt` (no extra hardware access), because XNU-style ADTs keep the boot-time NVRAM snapshot there [hypothesis: the node name and contents on j293 are not documented in the sources read; day-1 full ADT dump `adt-j293.txt` on the host can settle this at no cost]. Never call any NVRAM-writing helper.
- ADT facts to print for each block above: node path, `reg` entries (translated), `clock-gates`, `interrupts`, `compatible`, `sart-version` of `/arm-io/sart-ans`, and `/chosen/memory-map` entries (to intersect with the SART ranges and the guest RAM list).

### 5.6 What the output should answer
1. Is ANS2 powered (PS 0x3f0) and is its RTKit running (CPU_CONTROL bit 4, boot status 0xde71ce55)? Which SART ranges are programmed, and do they overlap guest RAM?
2. Is DCP running (CPU_CONTROL bit 4) or stopped, and are dart-disp0 and dart-dcp locked with the framebuffer mapped?
3. Which domains are left on that nothing in our design needs (candidates for a later, deliberate shutdown by the layer, never by Windows)?
4. DART mode (translate or bypass) for the PCIe and USB masters.
5. DWC3 mode **as seen over the proxy** (running, device mode; not the halted hand-over state) and the PHY words, for the T15 follow-up.

## 6. Risks for the layer and EDK2 now

Ordered by what can hurt the next hardware days (T11 EDK2 on top of the layer, then M3b).

1. **Running coprocessor with DMA into guest RAM (ANS2 and possibly others).** If iBoot left ANS's RTKit running, its SART-allowed buffers and any later firmware activity can write RAM that EDK2 or Windows already owns. Today the layer's guest RAM list excludes m1n1's reservations (`kboot_prepare_dt`), but nobody has intersected it with the SART entries. Until ehw2 has read them: treat the 0x802078000..0x9df45c000 range as **not proven free of ANS writes**, and do not start any storage experiment. Mitigation if confirmed: shrink the guest RAM by the SART ranges (cheap, in `stage1.c`), or have the layer do the RTKit sleep before the guest starts (needs RTKit code in EL2, see `re/m3b-ans-windows.md` section 5).
2. **Unknown DART state of the DMA masters (PCIe, USB, SIO).** The guest sees no IOMMU. If `dart-usb`, the PCIe DARTs or `sio_dart` are in translate mode with stale or empty tables, a Windows driver doing DMA with physical addresses faults; if in bypass, Windows has unrestricted DMA from every device (fine for a first boot, a security note later). m1n1 only ever configured dart-disp0, dart-dcp and dart-usb for its own use, `pcie_init` left the apcie DARTs alone. ehw2 section 5.3 answers it; nothing in EDK2 on day 3 needs DMA, so this blocks only M3b.
3. **The display chain is alive by inheritance, and fragile.** DCP is (probably) quiesced rather than off, dart-disp0 is locked, and `disp0_fe`/`disp0_cpu0` are left on only because Linux's own TODO says power management of them is not understood. Any code that resets or power-gates those domains, resets dart-disp0, writes DCP registers, or exposes the display to Windows through ACPI power methods blanks the only output we have. Keep the framebuffer a pure RAM-like window; never expose DCP or DISP0 in the guest FDT or ACPI. The brightness cannot be changed from our code either (SMC or DCP owns it): set it to maximum in macOS before the day (already in the day-2 notes).
4. **m1n1 left the watchdog disabled and PD IRQs masked.** The layer already re-arms the watchdog for reset (day 2, L1 twice, under 5 s). The PD controllers (CD3217) have their interrupt masks cleared by m1n1 and never restored on the kboot path: after the hand-over nobody services Type-C events; the charging and data-role behaviour of the two USB-C ports is therefore m1n1-dependent until a driver or the layer restores them [verified: src/usb.c:307, 353-424; effect on charging is **[hypothesis]**, not observed on day 2].
5. **Keyboard and trackpad are unavailable at first.** No coprocessor firmware is involved on the M1, but the SPI HID protocol is downstream-only; EDK2 starts without console input (design section 7), which is consistent. Do not assume iBoot's key polling leaves SPI3 usable.
6. **Do not "clean up" PMGR from the layer.** Powering down domains that look unused (the ones m1n1 enabled as parents) can take the SEP, SMC or PMP offline or break the display; any such step needs the full ehw2 map first and a separate decision.

## 7. Reuse recommendations (what to try, in order)

| Subsystem | Layer | EDK2 | Windows | Next step |
|---|---|---|---|---|
| Framebuffer | already | simple-framebuffer GOP (design section 8) | GOP framebuffer | none |
| Keyboard | no | later, SPI3 driver (downstream protocol) | no inbox driver [hypothesis] | research after T11 |
| USB host | no | maybe (needs host mode, PHY, PD) | inbox xHCI if firmware does the bring-up | T15 |
| ANS storage | measure only | no (RTKit needed) | native miniport or USB disk first | ehw2, then `re/m3b-ans-windows.md` option (c) |
| NVRAM | read only | none (RAM-backed variables) | none | macOS `nvram -p` once |

## 8. Open questions this note cannot settle (all answered by ehw2 or by two small macOS-side steps)
- Did m1n1 re-run the modeset on j293 (probable, section 2.1)? The console line "display: ..." of the next hardware day tells; keep the console log.
- Who holds the backlight level: SMC, panel, or NVRAM `backlight-nits` re-applied by iBoot? Test: macOS brightness slider vs `nvram backlight-nits`.
- Is ANS left running? Section 5.2.
- Is `/options` present in the j293 ADT and what does it contain? Read the existing host ADT dump.
- What does a read of a power-gated block do on the t8103 (fault or hang)? Only the gating rule keeps us safe; do not test it.
