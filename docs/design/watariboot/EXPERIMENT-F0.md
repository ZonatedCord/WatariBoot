# EXPERIMENT-F0: design of Gate B in QEMU (task A5)

- Project: Watari Boot · Date: 2026-10-12 · Status: **design, not executed** · Author: Sonnet executor (task A5), to be verified by Opus
- Links: [Phase 0 experimental ADR draft](../../adr/draft-phase0-experimental.md) · [ADR-0001](../../adr/0001-native-boot-without-hypervisor.md) · [ADR-0003](../../adr/0003-parking-protocol-instead-of-psci.md) · [ADR-0004](../../adr/0004-irq-instead-of-fiq.md) · [INTERFACES](INTERFACES.md) · [RISKS](RISKS.md) · [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) · [tools/acpigen](../../../tools/acpigen/README.md)

## 0. Conventions and constraints

- `[verified: source]` = read on the cited source during this task. `[hypothesis]` = not verified. `[hypothesis, to be confirmed with A1/A2/A4]` = depends on research A1 (HAL extensions), A2 (ACPI requirements and parking protocol), A4 (Raspberry Pi 3), not yet available.
- `[HUMAN]` = step performed by Marco, not by an agent.
- **Black box**: no Microsoft binary, symbol or dump is downloaded or analyzed by the project (Phase 0 ADR draft). Only the behavior of Windows is observed from the outside (bugcheck, debugger messages, boot outcome). The official ISO and WDK are *tools*, used without disassembling them.
- **Nothing in this document has been executed.** Before any download or installation the absolute Time Machine backup rule applies, with explicit confirmation from Marco (see sec. 5).
- All QEMU and `bcdedit` options cited are taken from the official documentation (links in sec. 9). Where an option was not found in the documentation it is stated.

## 1. What we want to know (Gate B questions)

| ID | Question | Variants that answer it |
|---|---|---|
| Q1 | Does our `acpigen` generator produce tables that Windows accepts like those of QEMU? (tool validation) | V1 |
| Q2 | Does Windows ARM64 enumerate the CPUs and reach boot if the MADT has the GICCs but **not** the GICD? | V2 |
| Q3 | Does Windows start the secondary cores with the Parking Protocol (mailbox in GICC) instead of PSCI? | V3 |
| Q4 | Does Windows load an HAL extension hooked to a CSRT group of ours? And with what interface/messages? | V4 |
| Q5 | How much does Windows depend on the architectural timer described in the GTDT (related to the fact that on the M1 the timer is FIQ-only, [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md))? | V5 |

The decisive question ("does it accept a non-GIC controller provided by an HAL extension?") has **no** QEMU variant that closes it on its own: see sec. 4 for the limits. The variants reduce uncertainty on its preconditions (V2, V3, V4, V5).

## 2. Environment

### 2.1 QEMU `virt` aarch64 with HVF

| Item | Choice | Status |
|---|---|---|
| Binary | `qemu-system-aarch64` (recent QEMU from Homebrew or built) | choice; installation [HUMAN] after backup |
| Machine | `-machine virt` | [verified: https://www.qemu.org/docs/master/system/arm/virt.html] |
| Accelerator | `-accel hvf` | `hvf` is among the accelerators listed by the invocation documentation, syntax `-accel name[,prop=value]` [verified: https://www.qemu.org/docs/master/system/invocation.html]; the virt page lists `host` as the only CPU model supported with HVF (besides KVM) and `max` = `host` for KVM/HVF [verified: virt page] |
| CPU | `-cpu host` (with HVF; equivalent to `max`) | [verified: virt page] |
| `gic-version` | values `2`, `3`, `4`, `x-5`, `host`, `max`. **There is no "none/disabled" value** | [verified: virt page] |
| Removing the GIC | **Not possible with `-machine virt`**: the GIC is always instantiated by the machine | [verified: no option to remove it on the virt page; the only related options are `gic-version`, `virtualization`, `msi`]. The "non-GIC" must therefore be simulated only in the **ACPI tables** (see V2) |
| GIC version to use | `gic-version=3` as the initial choice (it is the one used in QEMU examples for tpm-tis and consistent with the likely expectation of Windows) | [hypothesis, to be confirmed with A2: which GIC version Windows ARM64 requires]. If HVF does not accept v3 with the installed QEMU version: try `2` or `host`; the behavior of HVF with the GIC is [hypothesis]: check with `qemu-system-aarch64 -machine virt,help` and the release notes of the installed version |
| EL2 | `virtualization=off` (default) | [verified: virt page, default `off`]. Windows must run at EL1, as on the M1 without a hypervisor (ADR-0001) |
| `acpi` | `acpi=on` (or `auto`) | [verified: virt page, values `on/off/auto`] |
| `msi` | leave the default; if V2 runs into trouble with IORT/ITS, try `msi=off` | [verified: `msi` option documented; effect on Windows: hypothesis] |
| Memory / cores | `-m 8G -smp 4` (V3: `-smp 4` at least, better 8 to resemble the M1) | choice; the core limit with HVF depends on the host [hypothesis] |
| Firmware | `-drive if=pflash,format=raw,file=...` with EDK2 built by us (sec. 2.2) | pflash scheme [verified: TPM example in the QEMU documentation, https://www.qemu.org/docs/master/specs/tpm.html] |
| Graphics / input | `ramfb` or `virtio-gpu-pci`, keyboard and mouse `usb-kbd`/`usb-mouse` on `qemu-xhci` | [hypothesis] Windows has inbox drivers for xHCI and basic display; virtio-gpu has no inbox driver |
| Disk | `-device nvme` (Windows inbox driver) on the qcow2 image; ISO as `usb-storage` | [hypothesis]. Avoid `virtio-blk`, which requires the `virtio-win` drivers during setup |
| TPM | `swtpm` + `-chardev socket` + `-tpmdev emulator` + `-device tpm-tis-device,tpmdev=tpm0` | [verified: https://www.qemu.org/docs/master/specs/tpm.html]. Necessity for Windows 11: [hypothesis] required by setup (from A2/to be verified) |
| NIC for debugging | `-device e1000e,netdev=...` | KDNET supports Intel with device ID `10D3` [verified: Microsoft list below]; that QEMU's `e1000e` model presents exactly that ID: [hypothesis, check with `lspci`/Device Manager in the guest]. Alternatively `-device e1000` (ID `100E` is in the list) [hypothesis on emulated ID] |

KDNET list: https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/supported-ethernet-nics-for-network-kernel-debugging-in-windows-10 [verified: the page lists Intel 8086:10D3 and 8086:100E; for the Windows 11 ARM64 version one must check the WDK's `VerifiedNicList.xml`, as the page says].

Command line example (**scheme**, to be adapted; every option is among those verified above except where indicated):
```
qemu-system-aarch64 -machine virt,gic-version=3,acpi=on -accel hvf -cpu host -smp 4 -m 8G \
  -drive if=pflash,format=raw,file=FW_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,file=FW_VARS.fd \
  -serial <chardev> -display <to be chosen> ...
```
`-serial` and `-chardev` were not read in the documentation in this task (the page was truncated): **[hypothesis]** syntax to be confirmed at https://www.qemu.org/docs/master/system/invocation.html ("Character device options" section) before writing them into scripts.

### 2.2 EDK2 `ArmVirtQemu` built by us, with replaceable ACPI tables

**Verified fact that rules out the easy road**: QEMU's `-acpitable` option is documented **only for the i386 target** [verified: https://www.qemu.org/docs/master/system/invocation.html, "i386 target only"]. On `virt` aarch64 it is not available. Moreover with `acpi=on` QEMU generates by itself the MADT (with GICC, GICD and GICR), GTDT, DBG2, SPCR, IORT and the FADT with the PSCI flag [verified: QEMU `hw/arm/virt-acpi-build.c`: `build_madt`, `build_gtdt`, `build_dbg2`, `build_spcr`, `build_iort`; `ACPI_FADT_ARM_PSCI_COMPLIANT` for the SMC/HVC conduit]. The tables reach the firmware via fw_cfg.

**Where EDK2 plugs in** [verified: tianocore/edk2 `ArmVirtPkg/ArmVirtQemu.dsc`]: the platform includes `OvmfPkg/AcpiPlatformDxe/AcpiPlatformDxe.inf` (driver "QemuFwCfgAcpiPlatform", entry point `AcpiPlatformEntryPoint`), which reads the tables from fw_cfg and installs them via `EFI_ACPI_TABLE_PROTOCOL` (the module declares that protocol as a dependency); the DSC also has `OvmfPkg/PlatformHasAcpiDtDxe` and the PCD `PcdForceNoAcpi` (NV variable `ForceNoAcpi`) and a `TPM2_CONFIG_ENABLE` configuration.

Strategies to replace the tables, in order of preference:

| # | Strategy | Pros | Cons | Status |
|---|---|---|---|---|
| S1 | **Our own platform `WatariQemuPkg`** (minimal fork of `ArmVirtQemu`) with a DXE driver `WatariAcpiOverride`: after `AcpiPlatformDxe`, it walks the XSDT, and for each signature present in its Firmware Volume (`FREEFORM` files generated by `acpigen`) it replaces the QEMU table (uninstall + install via `EFI_ACPI_TABLE_PROTOCOL`, recomputing checksum and XSDT). The tables stay outside the code: they are binary files in the FV, regenerable without recompiling the driver | reproduces exactly the mechanism that `edk2-apple` will use (tables from generator -> ACPI protocol); no patch to QEMU | writing and testing the driver (about 1 agent-day of work, [hypothesis]); `UninstallAcpiTable` requires the `TableKey` handle returned at installation: if it is not available, the override must intercept the installation rather than replace after the fact [hypothesis: to be read in `MdeModulePkg/Universal/Acpi/AcpiTableDxe`] | proposal |
| S2 | Local patch to `AcpiPlatformDxe` that, for the "overridden" signatures, ignores the fw_cfg table and loads ours from the FV | a single point of modification | divergence from the upstream driver, to be redone at every update | proposal (fallback of S1) |
| S3 | `-machine virt,acpi=off` + firmware that installs **all** tables (DSDT included) from the generator | total control | requires a complete DSDT/FADT/IORT/MCFG/DBG2/SPCR: too much work for Gate B | discarded for now |
| S4 | Patch to the QEMU source (`virt-acpi-build.c`) | quick to write | breaks reproducibility, requires building QEMU | discarded |

**Tables that remain QEMU's** (we do not replace them): RSDP/XSDT, FADT (except V3), DSDT, MCFG, IORT, SPCR, DBG2, SRAT/PPTT if present. **Our tables**: MADT, GTDT, CSRT and, for V3, a modified FADT (PSCI bit) and for V4 an SSDT with the CSRT group device.

**Gaps in our `acpigen` relative to what is needed** [verified: reading of `tools/acpigen/include/acpigen.h` and `src/main.c`]:
1. The CLI accepts only `<madt|gtdt|pptt|csrt> -o file` and uses **only** the hardwired t8103 model (`t8103_cpus`, GICD absent in the CLI, 8 CPUs, `parked_address = 0`). There are no options to choose the model, number of CPUs, GICD, parking, GSIV.
2. `acpigen_build_madt` emits GICC and at most one GICD; it **does not emit the GIC Redistributor structure (type 0x0E)** that QEMU `virt` with GICv3 puts in the MADT (`build_append_gicr`) [verified: QEMU source]. The `gicr_base_address` field in the GICC exists but is a different mechanism.
3. There is no **FADT** or **SSDT/DSDT** generator.

Work needed on `acpigen` (task for an executor, not done here): (a) option `--model {t8103|qemu-virt}` and flags `--no-gicd`, `--parking`, `--ncpus`; (b) `qemu-virt` model with GICD/GICR base and timer GSIV **read from the DT/ACPI of the running QEMU** (not invented: see V1); (c) GICR structure; (d) FADT with configurable `arm_boot_arch`; (e) minimal SSDT with the `_HID`/`_UID` device bound to the CSRT group (compiled with `iasl`, see sec. 5) or alternatively hand-generated AML [hypothesis on feasibility].

**How to obtain the baseline values** (for V1, without inventing them): boot Windows or a UEFI shell with the stock QEMU tables and save them (EDK2 UEFI shell command `acpiview`/`dmpstore`: [hypothesis], check in the ShellPkg package), or read `virt-acpi-build.c` and `hw/arm/virt.c` for the GICD/GICR bases [verified: the files exist on github.com/qemu/qemu; the numeric values were not read in this task]. The binary comparison between QEMU tables and `acpigen` tables is the V1 test.

EDK2 build: requires an AArch64 toolchain (GCC cross or clang) and `python3`, `nasm`/`iasl` depending on the platform [hypothesis]; it is a **download/installation** so [HUMAN]: backup first.

### 2.3 Windows 11 ARM64 ISO

- Official Microsoft Windows 11 ARM64 ISO [HUMAN]: download from microsoft.com ("Download Windows 11 Arm64" page; exact URL to be verified by Marco at the time, not fixed here: [hypothesis]). Estimated size ~5-7 GB [hypothesis].
- Freeze the **build and SHA-256 hash** of the ISO used and record them in the log (R-11). Do not put the ISO in the repository: it goes in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (external disk; it must be connected) (project CLAUDE.md).
- License/activation: Windows without a key works in non-activated mode for testing; terms of use: [hypothesis, check the Microsoft terms] [HUMAN].
- Setup requirements (TPM 2.0, Secure Boot, RAM, disk): [hypothesis, to be confirmed with A2/Microsoft documentation]. Bypassing the setup checks is a community practice not documented by Microsoft: we do **not** use it if the swtpm alternative exists (above).

### 2.4 Estimated disk space (all estimates [hypothesis])

Free space on the Mac today: 53 GiB (from `df -h` on `/System/Volumes/Data`, [verified: executed in this task]).

| Item | Estimate |
|---|---|
| QEMU (Homebrew + dependencies) or built | 1-2 GB |
| EDK2 toolchain + edk2 tree + build | 3-6 GB |
| Windows 11 ARM64 ISO | 5-7 GB |
| Target qcow2 image (sparse; actually used) | 20-30 GB (nominal 64 GB) |
| Debugger VM (Windows ARM64 + WinDbg + WDK + Visual Studio Build Tools) | 40-60 GB |
| Snapshots/backups of images, logs, dumps | 5-10 GB |
| **Total** | **~75-115 GB** |

**Conclusion: 53 GiB free are not enough for the whole of Gate B** in the "debugger VM on the Mac" configuration. Options: external SSD for the images [HUMAN], free up space, or use an already available Windows PC as the debugger host (no space cost) [HUMAN, hypothesis that one exists]. Decision and space must be confirmed before downloading anything.

## 3. Debugging

### 3.1 Debug architecture

Windows is debugged with WinDbg, which runs only on Windows [hypothesis: no known macOS version; verify]. The project has no second computer (PLAN). Proposed scheme: two guests on the same Mac.

```
Mac (HVF)
 ├─ QEMU "target": Windows 11 ARM64 under test + our EDK2 + (kernel debug active)
 └─ "debugger" VM: Windows ARM64 (or other) with WinDbg + WDK  [HUMAN]
        link: virtual network between the two QEMUs (KDNET)  or  virtual serial
```

The target and the debugger VM connect with `-netdev socket` (point-to-point TCP, `listen`/`connect`) or with a shared QEMU network: **[hypothesis]** exact syntax and behavior to be read at https://www.qemu.org/docs/master/system/invocation.html (Network options section, not read in this task). Alternatively the debugger VM can be another virtualization product on the same network [HUMAN, hypothesis].

### 3.2 Debug transports

| Transport | How | Pros | Cons |
|---|---|---|---|
| **KDNET** | Supported NIC in the target (above) | fast, standard Microsoft, works with `bcdedit /dbgsettings net hostip:... port:...` | requires a NIC with an ID in `VerifiedNicList.xml`; depends on whether the ID emulated by QEMU is there [hypothesis]; requires the target's PCIe/MSI bus to work |
| **Serial (KDCOM)** | `bcdedit /dbgsettings serial debugport:1 baudrate:115200` | independent of PCIe/NIC; **DBG2 and SPCR are generated by QEMU** [verified: `build_dbg2`, `build_spcr` in virt-acpi-build.c] and Windows chooses the debug transport by reading DBG2 [verified: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables, DBG2 section] | QEMU's PL011 UART must be exposed to WinDbg: how (pipe/TCP) is [hypothesis]; compatibility of the UART type with KDCOM: [hypothesis, from A2/A4]; it is the realistic transport on the M1, so it is worth trying |
| Screen + dump only | bugcheck screen via QEMU, no debugger | zero infrastructure | minimal information; the QEMU command to capture the screen (monitor `screendump`) is [hypothesis, to be verified in the monitor docs] |

Plan: V0 and V1 with **both KDNET and serial tried** (if one does not attach it is known immediately, before blaming a variant). From V2 onward the transport that worked in V0 is used.

### 3.3 `bcdedit` commands (all to be run in the target guest, administrator prompt, then reboot)

Sources: https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--dbgsettings and https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--set [verified: syntax read in this task].

```
bcdedit /debug on                                       (enables kernel debugging for the current entry)
bcdedit /dbgsettings serial debugport:1 baudrate:115200
bcdedit /dbgsettings net hostip:<DEBUGGER_IP> port:50000 [key:<key>|newkey] [nodhcp]
bcdedit /set testsigning on                             (for V4: loads test-signed kernel code)
bcdedit /set nocrashautoreboot on                       (do not reboot after bugcheck: the code is read)
bcdedit /set bootlog yes                                (log of loaded drivers, Ntbtlog.txt)
bcdedit /set sos on                                     (shows driver names during boot)
(never set "quietboot on": the bcdedit /set page says it prevents seeing bugcheck data)
bcdedit /enum                                           (shows the entries and GUIDs)
```
Notes verified from the documentation: `/dbgsettings` configures but **does not enable** debugging, `/debug` is needed; the KDNET port must be 49152 or higher; with `net`, `HOSTIP` and `PORT` are required; for serial `DEBUGPORT` and `BAUDRATE` are optional (default COM1, 115200); before setting BCD options it may be necessary to suspend BitLocker and disable Secure Boot; `nointegritychecks` cannot be set with Secure Boot active; `testsigning` requires a reboot. The command to read `ForceNoAcpi` or other UEFI variables: not relevant.
**Secure Boot**: in the `ArmVirtQemu` EDK2 firmware the availability/state of Secure Boot is [hypothesis]; it must be decided and recorded for every run (it influences `testsigning`).

### 3.4 QEMU and firmware side logs

- Firmware serial: EDK2's PL011 UART console goes to `-serial` ([hypothesis on the syntax, see 2.1]); record every run to a file with a timestamp. Note: if the same UART is used by the Windows debugger, the firmware log and KDCOM contend for the port; use `virt`'s second UART (the QEMU documentation recommends edk2-stable202311 or newer for guests that use both UARTs [verified: search result on QEMU `virt` docs; to be rechecked on the page]) [hypothesis on the detail].
- Guest error message logs on the QEMU side (options `-d`, `-D`): [hypothesis, check the QEMU documentation before using them].
- For each run archive: full QEMU command line, firmware/tables hashes, ISO hash, QEMU version, serial log, WinDbg log (`.logopen`), final screenshot, bugcheck code with the four parameters.

### 3.5 What to look at in WinDbg (standard debugger commands, to be confirmed in the WinDbg documentation before use)

`!analyze -v` after a bugcheck, `.bugcheck`, `lm` (module list: does our HAL extension appear?), `!irql`, `!pcr`, `!ipi`, `!ioapic`/equivalent for ARM [hypothesis], `!acpi`/`!amli` for the tables. [hypothesis: existence and output of each on ARM64]. The document invents nothing else: the list is to be cleaned up after the first run (V0).

## 4. Variant matrix

### 4.1 What they prove and what they do not (honest note)

**In QEMU the GIC exists anyway** (`virt` cannot remove it, sec. 2.1). Consequences:

| Can prove | CANNOT prove |
|---|---|
| How Windows **treats the tables** when they are missing or inconsistent (GICD absent, parking, GTDT, CSRT): errors, bugchecks, handling of boot failures | That Windows works with a truly working **non-GIC** controller: QEMU's real GIC remains present and might be used by Windows even if the MADT does not declare it (MMIO accesses to known addresses) [hypothesis: unlikely but not excludable] |
| Whether Windows **loads** an HAL extension from CSRT and which calls/messages it emits (V4) | Whether the HAL extension can **register an interrupt controller** and have Windows use it: this would require an extension implementing a complete controller (bridge on QEMU's GIC = reimplementing a GIC driver, enormous work, [hypothesis]) |
| Whether Windows enumerates the cores and starts them with the **parking protocol** when the firmware parks them (V3), at least with the GIC present | The **wakeup without a GIC** (the parking protocol IPI is an SGI in the GIC world, see INTERFACES sec. 4): on the M1 it will be AIC; in QEMU the SGI comes from the real GIC (V3 uses it, so it proves nothing about the AIC case) |
| That Windows ARM64 **runs on Apple M1 cores** at EL1 (with HVF the guest runs on the real cores): useful for R-07 (ID registers, page granularity, instructions) [hypothesis: HVF masks/exposes some registers, so the proof is partial] | The behavior with the M1's **FIQs**: QEMU `virt` does not route the timer as an AIC FIQ. The critical constraint of [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) (FIQ-only timer) is not reproduced |
| The **process** (fast iteration, debug tools, table generator, override DXE driver) | Timing, SError, cache coherency, DART, ANS, PMGR, real AIC: everything that is M1 hardware |

So Gate B can give **a strong negative verdict** (if Windows already rejects the preconditions in the tables, the road is in doubt), but only a **weak positive verdict** ("the preconditions in the tables are accepted"). The definitive yes comes only from the hardware (Phase 3) or from A1/A2/A4 combined with V4.

### 4.2 Table

Convention: "QEMU baseline" = tables generated by QEMU `virt` with `gic-version=3` and `acpi=on`. The `acpigen` commands are those **to have after the extension of sec. 2.2** (the `--model/--no-gicd/--parking/...` options do not exist yet: today the CLI accepts only `acpigen <madt|gtdt|pptt|csrt> -o file`, [verified: `src/main.c`]).

| Var. | ACPI tables used | `acpigen` command (future, except where indicated) | What we observe | Possible outcomes -> conclusion | Estimated time |
|---|---|---|---|---|---|
| **V0** baseline | all of QEMU's, our EDK2 firmware **without** override | none (`WatariAcpiOverride` disabled) | Windows installs and reaches the desktop; all cores visible (`-smp`); KDNET and/or serial attached; `bcdedit` applied; image snapshot | **OK**: valid environment, proceed. **Windows does not start / does not install**: environment problem (HVF, firmware, TPM, disk driver), not Watari's: solve it first of all, no conclusion on Q1-Q5. **Debug does not attach**: change transport before V1 | 1-3 days (installation, configuration, two debug transports) [hypothesis] |
| **V1** generator | MADT, GTDT replaced by `acpigen` equivalents of the QEMU baseline (same CPUs, GICC, GICD, GICR, timer GSIV, flags); rest QEMU | `acpigen madt --model qemu-virt --ncpus N -o madt.bin`; `acpigen gtdt --model qemu-virt -o gtdt.bin` | Binary comparison with the QEMU tables (same fields, valid checksums); Windows behaves like V0 | **Like V0**: `acpigen` and `WatariAcpiOverride` validated, V2-V5 are interpretable. **Differences in boot or bugcheck**: bug in the generator or the override (not in Windows): fix and redo. This is the test that separates "Windows rejects" from "our table is wrong" (false negative, sec. 6) | 1-2 days, plus the work on the generator [hypothesis] |
| **V2** GICC without GICD | MADT with 4-8 GICC and **without GICD (and without GICR)**; GTDT and the rest as V1 | `acpigen madt --model qemu-virt --no-gicd --ncpus N -o madt.bin` | Where the boot stops; bugcheck code and parameters; messages on serial/debugger; whether Windows enumerates the CPUs before failing; whether the phase is before/after `HalInitSystem` [hypothesis on names] | **Complete boot or halt in a way "not due to the controller" (e.g. no interrupt but no rejection)**: signal that the absence of GICD is not checked early -> R-02 scaled down (weak positive, see below). **Early bugcheck/halt with a cause traceable to the absence of an interrupt controller**: Windows demands a declared GIC -> R-02 confirmed; the plan moves to the "dummy GIC in the DSDT/ACPI intercepted by extension" (INTERFACES 3.1) and to plan B. **Generic rejection not attributable**: inconclusive, repeat with V2b (GICD present but invalid address) to separate "the structure is missing" from "the hardware is missing" | 2-4 days [hypothesis] |
| **V3** parking protocol | MADT: GICC with valid `parking_version` and `parked_address` for each core, **GICD present** (to isolate the question); FADT with `arm_boot_arch` **without** PSCI_COMPLIANT (QEMU sets it with SMC/HVC, [verified: virt-acpi-build.c]); rest V1 | `acpigen madt --model qemu-virt --parking --parked-base 0x... -o madt.bin`; `acpigen fadt --model qemu-virt --no-psci -o fadt.bin` (FADT: to be created) | Whether Windows starts the secondary cores (`!pcr`/Task Manager: N active cores); whether it writes to the mailbox (observable with a watchpoint or the parking DXE driver's log); any timeouts/bugchecks; write sequence (entry point before cpu_id) to compare with [verified: Linux, `acpi_parking_protocol.c` as in INTERFACES 4] | **Secondary cores started**: Windows supports the parking protocol as expected and our mailbox is compliant (positive, but with the GIC present, see limits). **Windows stays at 1 core without errors**: the MADT/FADT was not interpreted as parking, or Windows requires PSCI; recheck the FADT flags and the protocol version (**hypothesis, from A2**). **Bugcheck at core startup**: anomaly in the mailbox, unmapped address, unreserved memory (UEFI memory map) -> fix and repeat. **Never writes the mailbox**: Windows does not use the protocol -> ADR-0003 in doubt | 4-7 days: the EDK2 parking code is needed (cores in QEMU start powered off and must be turned on with PSCI `CPU_ON` from the firmware and put into a polling loop, [hypothesis on the QEMU/PSCI mechanism]) |
| **V3b** (optional) parking + no GICD | as V3 with the V2 MADT | `--parking --no-gicd` | as V3 | only if V2 and V3 are both non-negative | 1-2 days |
| **V4** CSRT + test HAL extension | MADT/GTDT as V1; **CSRT** with a resource group of ours (our vendor ID, not Microsoft-reserved); **SSDT** with the ACPI device linked to the group (required by the Microsoft document: every resource group must have a device in the namespace with matching identifiers, [verified: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables, CSRT section]) | `acpigen csrt --model watari-test -o csrt.bin` (the current `acpigen csrt` CLI generates the "APLE" group of the t8103 model, [verified: `t8103_model.c`]); SSDT: to be created | Whether Windows attempts to load the DLL (`lm` shows the module; `DbgPrint` message from the DLL; entry in `Ntbtlog.txt`); if loading fails, with what error; whether the ACPI device is enumerated; whether `testsigning` is enough or the DLL is rejected; where in the boot it happens | **DLL loaded and our entry point executed**: the CSRT -> HAL extension mechanism works with a test signature for our code (positive for Q4; reveals the real shape of the interface, to be compared with A1). **Not loaded**: causes to be separated — (a) wrong CSRT/SSDT (V1-like validation), (b) CSRT group format or INF/registry association different from the assumed ones, (c) Windows loads extensions only for resource types it knows -> conclusion on the "documentation gap"; the project must then obtain the information from A1/WDK, not guess. **Loaded but Windows bugchecks on the first call**: excellent informative signal: the interface exists, the parameters are wrong | 5-10 days: building the DLL requires a Windows VM with WDK/SDK [HUMAN]; depends heavily on A1 for entry point and signing [hypothesis, to be confirmed with A1] |
| **V5** timer | MADT as V1; **GTDT** (a) absent, (b) with invalid GSIV, (c) with altered flags/PPI; rest V1 | (a) no GTDT file: the override *removes* the table; (b),(c) `acpigen gtdt --model qemu-virt --virt-gsiv N ...` (options to be created) | Whether Windows starts without GTDT; whether the system tick works (clock, `Get-Date`, `!pcr`); messages/bugchecks; how long before the system hangs; effect on the `QueryPerformanceCounter` counter vs. timer interrupts | **(a) Windows starts and time flows**: Windows has an alternative tick source (other than the GTDT) usable without an ACPI description: if a non-GIC tick exists, it reduces R-03 [hypothesis, should be separated from what QEMU/HVF exposes]. **(a) Windows does not start / time frozen / "clock" bugcheck**: the architectural timer described in the GTDT is mandatory and is received as a GIC interrupt; on the M1 (FIQ-only timer, [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md)) the only way left is an HAL timer extension or plan B -> R-03 remains critical and A1 is searched for the "timer from HAL extension" mechanism. **(b)/(c)**: measures tolerance to GSIVs inconsistent with the controller (useful for the GSIV addressing chosen for the AIC). **Note**: V5 is not the FIQ constraint; for that there is no proof in QEMU (4.1) | 2-4 days [hypothesis] |
| **V6** timer as FIQ | tables as V1 (V1 MADT/GTDT); modified firmware or QEMU to deliver the timer (INTID 30 and 27) as FIQ: option A = DXE driver that puts the PPIs in Group 0 and enables `ICC_IGRPEN0_EL1`; option B = `virt.c` patch with the timer output wired to `ARM_CPU_FIQ` | none for `acpigen`; `-accel hvf,kernel-irqchip=off` (fallback `-accel tcg`), details in 4.4 | Bugcheck, hang, absence of clock tick, complete boot with advancing clock; trace of the delivered FIQs | **Works**: Windows handles the timer FIQ, decisive for the timer strategy (ADR-0004, R-03). **Bugcheck/hang**: does not tolerate the FIQ, tick via IRQ or plan B is needed. **No tick**: FIQ ignored. **FIQ not demonstrated**: inconclusive. Detail in 4.4.4 | 3-4 days [hypothesis] |

### 4.3 Cross-cutting dependencies and uncertain points

- **What "reaches the desktop" means**: measurable criteria for each run: (1) logo/spinner, (2) login/OOBE screen, (3) desktop with Task Manager, (4) PowerShell responds; the last milestone reached is recorded. If the boot stops early, the debugger is read first of all.
- **IORT/ITS and MSI** (QEMU tables) may refer to the GIC/ITS even when the MADT does not declare them; if V2 stops because of this, the cause is not "Windows demands the GIC in MADT" but "inconsistent ACPI": try `msi=off` [verified that the option exists; effect: hypothesis].
- **Difference between "accepts" and "uses"**: for each variant note whether the bugcheck happens before or after the first call to the controller code (in V4 for the extension).
- Which internal function names to cite (e.g. `HalpInterruptRegisterController`) comes from PLAN/INTERFACES but is [hypothesis]: they are not used as an observation criterion; the criteria are the externally visible behaviors.

### 4.4 V6: timer delivered as FIQ

Task A10 (issue #18). Purpose: on the M1 the ARM timer arrives only as FIQ ([re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) sec. 2). V5 does not reproduce it (4.1): in QEMU the timer is a GIC PPI delivered as IRQ. V6 asks, as a black box: **does Windows ARM64 tolerate or handle a timer FIQ?** QEMU sources read in this task on `master` of github.com/qemu/qemu (the version installed by Marco may differ: recheck the indicated files).

#### 4.4.1 How to make a PPI arrive as FIQ in a GICv3 (QEMU)

- **Delivery rule.** In `gicv3_cpuif_update()` (`hw/intc/arm_gicv3_cpuif.c`): a Group 0 interrupt is always signaled as FIQ (`case GICV3_G0: isfiq = true`); a Group 1 NS one is signaled as IRQ when the CPU is non-secure. The code comment refers to sec. 4.6.2 of the GICv3 specification [verified: arm_gicv3_cpuif.c, `gicv3_cpuif_update`]. The Arm specification was not re-read directly: [hypothesis] that sec. 4.6.2 says the same thing.
- **PPI group.** `gicv3_irq_group()` (`gicv3_internal.h`): for INTID < 32 the bit of `GICR_IGROUPR0` set to 1 means Group 1 NS; at 0, with `GICD_CTLR.DS=1`, it means **Group 0** [verified]. The NS EL1 physical timer PPI is INTID 30 (`ARCH_TIMER_NS_EL1_IRQ`), the virtual one INTID 27 (`ARCH_TIMER_VIRT_IRQ`), the NS EL2 hyp timer INTID 26 [verified: `include/hw/arm/bsa.h`].
- **Single-security-state GIC.** If `virt` does not have `secure=on`, the GIC's `has-security-extensions` property is false and at reset `GICD_CTLR = DS | ARE` [verified: `arm_gicv3_common.c` reset; `virt.c` lines 1417-1419 with `has-security-extensions = vms->secure`]. With DS=1 a non-secure guest can write `GICR_IGROUPR0` [verified: `arm_gicv3_redist.c`, `case GICR_IGROUPR0` writes only if `attrs.secure || DS`]. So **neither `secure=on` nor `virtualization=on` is needed**: `gic-version=3` and a write to `GICR_IGROUPR0` with bit 30 (and 27) at 0 suffice. `secure=on` would add an EL3 and the `SCR_EL3.FIQ` rule (the FIQ would go to EL3, for which there is no firmware to handle it): to be avoided [hypothesis, reasoning from the Arm model; not proven].
- **Enables needed for the FIQ to fire.** `icc_hppi_can_preempt()` requires `icc_igrpen[grp] != 0`, i.e. **`ICC_IGRPEN0_EL1`=1** for Group 0 [verified: arm_gicv3_cpuif.c:991]. With DS=1 `GICD_CTLR.EnableGrp0` (bit 0) is writable [verified: arm_gicv3_dist.c, `case GICD_CTLR`, mask `EN_GRP0 | EN_GRP1NS`]. The PPI enable in `GICR_ISENABLER0` and suitable priority/PMR are also needed (as for any interrupt).
- **Group reset.** At reset `gicr_igroupr0 = 0` (Group 0) unless `irq_reset_nonsecure`, set only for direct boot of a kernel with a security-aware GIC [verified: arm_gicv3_common.c lines 512-526, 574-600].
- **Who configures it: our EDK2.** EDK2's `ArmPkg/Drivers/ArmGicDxe/GicV3/ArmGicV3Dxe.c` driver, if `GICD_CTLR.DS=1`, writes `0xffffffff` to the IGROUPR register of the SGI redistributor frame (everything in Group 1 NS) and does the same for the SPIs [verified: ArmGicV3Dxe.c, "Disable Security (DS)" block; the write concerns the `mGicRedistributorBase` redistributor of the CPU running the driver: on the secondary cores the value stays the reset one, i.e. Group 0 [hypothesis]]. For V6 a step must be added, **after** that driver and for **every** CPU, that clears the PPI bit and enables Group 0 (see 4.4.3). TF-A is not involved in `virt` without `secure=on`.

#### 4.4.2 HVF or TCG

- With HVF `gic-version=3` is accepted [verified: `virt.c` ~2711-2717]. But the HVF irqchip has two modes: **in-kernel** (Hypervisor.framework's GIC, `hv_gic_create`) and **userspace** (GIC emulated by QEMU). The option is `-accel hvf,kernel-irqchip=on|off|split` (split not supported) [verified: `accel/hvf/hvf-all.c` and `hvf-accel-ops.c`]. In `virt` machines up to 11.0 the default for HVF is userspace; for later ones it is in-kernel [verified: `virt.c` `get_kernel_irqchip_default` and `hvf_no_kernel_irqchip_default = true` in `virt_machine_11_0_options`]. The default therefore depends on the QEMU version: **it must always be made explicit**.
- **Apple's in-kernel GIC is not under our control**: whether it respects IGROUPR/Group 0 as per the specification is unknown [hypothesis]. **Recommended: `kernel-irqchip=off`**, i.e. the QEMU GICv3 code read above. In that mode HVF injects a FIQ into the guest with `hv_vcpu_set_pending_interrupt(..., HV_INTERRUPT_TYPE_FIQ, ...)` when the QEMU CPU has `CPU_INTERRUPT_FIQ` [verified: `target/arm/hvf/hvf.c`, `hvf_inject_interrupts`], and the virtual timer goes from `HV_EXIT_REASON_VTIMER_ACTIVATED` to `qemu_set_irq(gt_timer_outputs[GTIMER_VIRT])` [verified: hvf.c lines ~2580-2583]. The physical timer (CNTP_*) in userspace mode is emulated by QEMU (accesses trapped [verified: hvf.c, handling of `SYSREG_CNTP_*`]). That the whole path works for a timer FIQ in practice: [hypothesis, to be proven].
- **TCG** (`-accel tcg`, `-cpu max`) runs the same GIC model and the same FIQ logic without depending on Hypervisor.framework and is slower (orders of magnitude, not measured); for a Windows boot test it may be very slow [hypothesis]. It is the fallback if HVF userspace does not inject the FIQ.
- **Most likely command** [hypothesis: assembled from verified options, not executed]:
```
qemu-system-aarch64 -machine virt,gic-version=3,acpi=on,secure=off,virtualization=off \
  -accel hvf,kernel-irqchip=off -cpu host -smp 4 -m 8G \
  -drive if=pflash,format=raw,file=WATARI_FW_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,file=WATARI_FW_VARS.fd ... (rest as V0)
```
  Fallback: `-accel tcg -cpu max`. Tracing options to see the FIQs on the QEMU side: events `gicv3_cpuif_set_irqs` and `hvf_inject_fiq` exist in the code [verified: `trace_gicv3_cpuif_set_irqs`, `trace_hvf_inject_fiq`]; the `-trace` syntax is [hypothesis, check the documentation first].

#### 4.4.3 How to force it without modifying Windows (options, evaluated)

| Opt. | Method | Pros | Cons | Verdict |
|---|---|---|---|---|
| **A** | Patch to our EDK2 (DXE driver `WatariTimerFiq`, after `ArmGicV3Dxe`): on every CPU clears bits 27 and 30 of `GICR_IGROUPR0`, sets `GICD_CTLR.EnableGrp0` and `ICC_IGRPEN0_EL1=1`. No patch to QEMU | No change to QEMU; reproduces a real firmware operation | **Windows reprograms the GIC**: its GICv3 driver probably sets the PPIs back to Group 1 (and enables only IGRPEN1) [hypothesis, not verified: Windows is not analyzed]. In that case the FIQ vanishes before arriving and V6 would give a false "normal IRQ" result. It must be checked by reading IGROUPR0 after boot (via the debugger or a QEMU breakpoint) | Try it first: low cost, and whether Windows cancels it or not is already a data point |
| **B** | QEMU patch in `hw/arm/virt.c` (~lines 1293-1307): for `GTIMER_PHYS` and `GTIMER_VIRT` wire the timer output **directly** to `qdev_get_gpio_in(cpudev, ARM_CPU_FIQ)` instead of the GIC PPI (the GIC FIQs are wired to the same input, line ~1326: a 2-input OR gate is needed) | **Independent of what Windows does to the GIC**; it is the model most faithful to the M1, where the timer "bypasses AIC. It is wired straight to FIQ" ([re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) sec. 0) | Requires building QEMU (download/installation, backup); the change is ours, ~10 lines, but must be kept aligned; the ACPI GTDT still declares GSIV 30/27: to decide whether to leave it [hypothesis: Windows uses it to choose/validate the timer]. Under HVF in userspace the FIQ goes through the `hvf_inject_interrupts` path: [hypothesis] works | **Recommended as the definitive method** if A is cancelled by Windows (or by default, because it does not depend on Windows) |
| **C** | QEMU patch in the GIC model: force Group 0 for INTIDs 26-30 ignoring `GICR_IGROUPR0` (`gicv3_irq_group`) | Keeps the "via GIC" path (IGRPEN0 still needed) | More invasive and less faithful: hides from the guest its own write to IGROUPR; keeps a dependence on `ICC_IGRPEN0_EL1` that Windows probably does not enable [hypothesis] | Discarded (B is simpler and more faithful) |
| **D** | `secure=on` with TF-A/EDK2 routing via `SCR_EL3.FIQ` | Uses a "standard" Arm configuration | Requires EL3 firmware that handles the FIQ and forwards it to EL1: big job, not what happens on the M1 | Discarded |

Outcome of the evaluation: **A as a first low-cost attempt, B as the reference method.** With A, if the result is "all normal" one must first prove that the FIQ really arrived (trace `hvf_inject_fiq` or `gicv3_cpuif_set_irqs ... fiqlevel=1`), otherwise it is not a result. With B the FIQ delivery is guaranteed by the machine, so the result is interpretable.

#### 4.4.4 What to observe and how to read the outcomes

Record, as in the other variants (sec. 3.4): QEMU command, QEMU version, ISO hash, Windows kernel build, `hvf_inject_fiq` / `gicv3_cpuif_set_irqs` traces, serial/debugger log, screenshots. Precondition: V0 and V1 green (same rule as V5).

| Observation | How | Reading |
|---|---|---|
| **a. Bugcheck** | code and parameters; after `bcdedit /set nocrashautoreboot on`; `!analyze -v` | A bugcheck tied to an unhandled exception or an unexpected interrupt (exact code names: [hypothesis], read them, do not guess) -> **Windows does not tolerate the FIQ**: no useful FIQ vector/handler. |
| **b. Hang without bugcheck** | frozen screen, no I/O, the debugger receives nothing | Possible FIQ asserted and never serviced (storm/loop): Windows does not silence the source -> "not handled". Distinguish from a hang for other reasons with V0/V1 and with the counter of injected FIQs in the QEMU trace (grows continuously = storm). |
| **c. No clock tick** | boot that proceeds but clock frozen, `Get-Date` does not advance, threads not scheduled with timeouts, tick DPCs absent | Windows ignores the FIQ and does not receive the tick via IRQ: the FIQ is "tolerated but useless" for the timer. Does not conclude on the handling of the FIQ itself: the timer remains in effect unusable. |
| **d. Works** | complete boot, advancing clock, Task Manager, and the trace shows delivered FIQs | **Windows handles the timer FIQ** (or maps it onto an internal path): see below. Verify that the FIQs are really delivered and not just programmed (otherwise false positive, E-3-like). |
| **e. Debugger use** | breakpoint on the guest FIQ vector (`VBAR_EL1 + offset` of the AArch64 vector table for FIQ from EL1: calculation to be verified in the Arm specification [hypothesis]), `.bugcheck`, `lm`, `!pcr`, `!irql` (existence on ARM64: [hypothesis]) | If the debugger is attached (serial/KDNET), the Windows kernel will stop at the bugcheck or remain in break: compare the paths with V0. If debugging changes the result, record it (E-6). |

Conclusions per outcome:
- **d (positive)**: **decisive for the timer strategy.** It means that Windows ARM64 accepts a timer FIQ (at least in QEMU, with this Windows version): the "never FIQ" strategy ([ADR-0004](../../adr/0004-irq-instead-of-fiq.md)) and the need for an alternative timer via IRQ (watchdog IRQ 338, [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) sec. 3) would lose urgency: the M1's AIC/FIQ could be mapped natively, with an HAL extension that handles the FIQ or with a transparent AIC. It remains to be demonstrated on the M1 hardware and repeated on more Windows builds (R-11).
- **a or b (negative)**: Windows does not tolerate the timer FIQ (only under QEMU conditions). Confirms R-03/V5: another tick source via IRQ is needed (HAL timer extension with WDT or `ACC_OVRD`) or plan B (EL2 shell that converts FIQ into a virtual IRQ, contrary to ADR-0001). Caution: with method A, first rule out that Windows' GIC configuration cancelled the FIQ; with B the negative is clear-cut.
- **c**: the FIQ is ignored; for Windows the timer does not exist -> same treatment as a/b for the strategy, but "softer": it indicates that Windows survives a spurious FIQ and that the only problem is giving the tick via IRQ.
- **Inconclusive**: if it is not demonstrated that the FIQ was delivered (trace absent), or if V0/V1 are not green, or if HVF userspace does not inject correctly (switch to TCG and repeat).

#### 4.4.5 Limits of V6

- QEMU models a "pure" GICv3 FIQ, **not** the behavior of the M1's AIC (no global status register, multiple sources, masking via `CNTP_CTL_EL0.IMASK`, [re/f0-m1-fiq-irq.md](../../../re/f0-m1-fiq-irq.md) sec. 1). Even a positive outcome does not prove that Windows handles the M1's other FIQs (fast IPI, PMU): only the FIQ -> timer handler path.
- HVF's in-kernel GIC is not modifiable; with `kernel-irqchip=off` the performance and timing behavior of the guest may differ [hypothesis].
- With A the test depends on Windows' reprogramming of the GIC; with B it is bypassed, but QEMU is modified (non-standard binary, build to be reproduced). With B, ACPI GTDT/MADT/GICD may remain inconsistent with the FIQ: Windows might refuse because of this and not because of the FIQ (see E-1/E-2).
- Which timer Windows ARM64 uses (physical CNTP, virtual CNTV, EL2) is not documented here [hypothesis]: route **both** as FIQ (INTID 30 and 27).
- Estimated cost: A about 2 days (DXE driver + trials), B about 1-2 days plus building QEMU [hypothesis]. Downloads/installations: only after a Time Machine backup with explicit confirmation from Marco.

## 5. Human prerequisites [HUMAN]

All the steps below require **first**: complete Time Machine backup, explicit confirmation from Marco in chat, free space confirmed (sec. 2.4). Sonnet agents do not download or install anything.

| # | Step | Details |
|---|---|---|
| H1 | Time Machine backup | Already confirmed for Phase 0 in `docs/STATUS.md` (commit "Backup Time Machine confermato"), **to be reconfirmed before every new installation** [absolute rule] |
| H2 | Disk space | Decide where the images live (external SSD, freed internal disk, or Windows PC as debugger host). Goal >= 100 GB available, see 2.4 |
| H3 | Install QEMU | e.g. Homebrew (`brew install qemu`) [hypothesis on the command, check the formula], or build; verify with `qemu-system-aarch64 --version` |
| H4 | Install the EDK2 toolchain and build the firmware | steps carried out by an executor, after confirmation |
| H5 | Download the Windows 11 ARM64 ISO | official Microsoft site; record version/build and SHA-256; save it outside the repo in `/Volumes/Arthur Fonzarelli/WatariBoot-data/` (external disk; it must be connected) |
| H6 | Development Windows with WDK and debugger | a Windows ARM VM (or Windows PC): Windows SDK, WDK, Visual Studio Build Tools to build the V4 DLL and to use WinDbg. Software to be downloaded from Microsoft sites [HUMAN]. Virtualization tool for the VM on the Mac: Marco's choice [hypothesis: UTM/Parallels/VMware; each has its own licenses and prices, to be verified] |
| H7 | Licenses | Windows (non-activated for testing: verify the terms [hypothesis]); virtualization tool for the debugger VM; WDK/SDK/VS Build Tools: Microsoft licenses to be read and accepted **by Marco**. Acceptance of terms is not done by an agent |
| H8 | `swtpm` | for the virtual TPM; installation [HUMAN]; QEMU syntax verified (2.1) |
| H9 | `iasl` (ASL compiler) | for the V4 SSDT; it is a download [HUMAN]; alternative: SSDT generated by `acpigen` (to be developed) |
| H10 | Test signature | no purchase: `bcdedit /set testsigning on` in the target guest; test certificate generated with the WDK tools (procedure from A1/Microsoft documentation) |
| H11 | Microsoft account/login | Windows may require an account at OOBE: Marco's choice [hypothesis: the no-account mode depends on the build]. Agents do not enter credentials |

## 6. Execution order and stop criteria

Order: **V0 -> V1 -> V2 -> V3 -> V5 -> V4** (V3b after V2/V3). V4 is last because it depends on A1 and on the Windows toolchain and because it has the highest cost; V5 is cheap and informative, so it is done earlier.

| Step | Prerequisite | Success criterion | Stop criterion |
|---|---|---|---|
| 0 | H1-H5 | Windows installed and working under QEMU+HVF; snapshot "V0-ok" | If after ~5 days V0 does not start: **stop** Gate B, reassess the environment (TCG instead of HVF, another QEMU version) before touching the tables |
| V1 | DXE override and extended `acpigen` | tables identical to QEMU's for the fields of interest, boot equal to V0 | If V1 differs from V0 and the cause is not found in ~3 days: **stop**, no later variant is interpreted (the generator or override are not reliable) |
| V2 | V1 green | clear outcome (boot or attributable bugcheck) | If for three independent repetitions the outcome is not distinguishable (generic rejection): freeze the result as "inconclusive" and move on to V3 without insisting |
| V3 | V1 green + parking driver | N cores active, mailbox written | If the EDK2 parking driver does not work in QEMU within ~1 week: suspend V3 and note it; parking will have to be written in `edk2-apple` anyway |
| V5 | V1 green | outcome for (a), (b), (c) | none specific |
| V4 | V1 green, A1 available, H6, H9, H10 | DLL loaded or reason for non-loading identified | If A1 information on the interface is missing: **do not guess**: V4 is suspended and A1 is awaited |

**Global stop of Gate B** (any of these): (1) two consecutive variants (V2 and V3) with bugchecks indicating that Windows demands GIC/PSCI -> verdict "native road in doubt", move to discussion with Marco on plan B (ADR-0001); (2) exhaustion of the agreed resources (total time > ~4 weeks of agent work, [hypothesis]). **All verdicts go to Marco**, no automatic change of strategy.

Total estimate if everything goes smoothly: 3-5 calendar weeks (shared work agents + Marco, who runs downloads and steps in the VM) [hypothesis]; the bottlenecks are V0 (Windows + debug environment) and V3 (parking code).

## 7. Experiment risks and interpretation of false negatives and false positives

| ID | Risk | Error type | How it is controlled |
|---|---|---|---|
| E-1 | Badly generated table (checksum, length, fields) or DXE override that installs badly | **False negative** (Windows seems to reject but it is our fault) | V1 mandatory before any other; check with an ACPI decoder (e.g. `iasl -d`) and binary comparison with QEMU; V1 outcome equal to V0 |
| E-2 | Other QEMU tables (IORT/ITS/MCFG/DSDT/FADT) that refer to the GIC or PSCI and thus produce errors not due to the MADT | **False negative** for V2/V3 | Look at the cause of the bugcheck, not just the fact; V2b; attempt `msi=off`; for V3 the FADT without PSCI is part of the variant |
| E-3 | QEMU's real GIC is active anyway: Windows might work "by accident" by accessing the GIC without it being in the MADT | **False positive** for V2/V3b | Record the MMIO accesses to the GIC region (QEMU tracing: options to be documented [hypothesis]); do not conclude "Windows accepts the non-GIC" but only "accepts a MADT without GICD" |
| E-4 | Different firmware/image between runs (no reproducibility) | both | QEMU command, versions, hashes and tables archived; disk snapshot before every run (`qcow2`, [hypothesis on the workflow]); only one variable changes at a time |
| E-5 | HVF presents the guest with a CPU different from the real M1 (masked registers, EL1 instead of EL2, no FIQ/AIC) | **False positive** on CPU/timer compatibility | State the limit in the results; V5 proves nothing about the FIQ constraint |
| E-6 | Debugger does not attach or alters the behavior (KDNET/serial change the boot) | false negative / interference | try V0 with and without debug; compare timings and path; if debugging changes the result, record it |
| E-7 | Windows updates change the behavior (R-11) | both | Freeze the ISO, do not run Windows Update; record the kernel build in the log of every run |
| E-8 | V4: the extension does not load for signing reasons (testsigning, Secure Boot active, chains) and not because of CSRT | **False negative** | Verify in V0 that any test-signed test driver loads with `testsigning on`; note the Secure Boot state |
| E-9 | Information from A1/A2/A4 contradicts the assumptions of V3/V4 (e.g. protocol version, CSRT resource type) | both | Rewrite the sections marked [hypothesis, to be confirmed with A1/A2/A4] before execution; do not run V3/V4 without A2/A1 |
| E-10 | Risk to the Mac: none direct (all in VM), but downloads are risky for space and integrity | operational | backup (H1), space (2.4), downloads only from official sources with the hash recorded |
| E-11 | Cost/time > benefits: Gate B is long and with a weak positive verdict | strategic | decide with Marco after V1-V2: if V2 is clearly negative, the value of the other experiments changes |

Rule for reading results: a **positive** outcome in QEMU is written "the table preconditions were not rejected in QEMU" and **not** "the M1 will work". A **negative** outcome is written "Windows rejects this table variant in QEMU with this cause" only after V1 and the E-2 check are green.

## 8. Expected outcome and decisive variant

| Question | Variant | Most informative outcome |
|---|---|---|
| R-02 / Q2 | **V2** | Bugcheck clearly tied to the absence of GICD -> Windows demands a GIC in the MADT (high impact: "dummy GIC" fallback or plan B). Complete boot -> the constraint is not in the tables |
| R-06 / Q3 | **V3** | Cores started with the mailbox and without PSCI -> ADR-0003 holds on the Windows side; the wakeup via AIC remains open |
| Q4 | **V4** | DLL loaded from CSRT with the entry point executed -> the only experiment that approaches the decisive question (but does not close it) |
| R-03 / Q5 | **V5(a)** | Windows does not start without GTDT -> the architectural timer is mandatory, so either an HAL timer extension or plan B |

The **decisive result against** the native road would be: clear-cut negative V2 *and* (V4 not loadable with A1 information confirming the impossibility). The **decisive result in favor** (only as a precondition) would be: V2 and V3 positive, V4 loads the DLL and A1 confirms the controller registration interface; final confirmation remains the hardware (Phase 3).

## 9. Sources consulted in this task

- QEMU `virt`: https://www.qemu.org/docs/master/system/arm/virt.html (gic-version, virtualization, acpi, msi, CPU, HVF).
- QEMU invocation: https://www.qemu.org/docs/master/system/invocation.html (`-acpitable` i386 only; `-accel`).
- QEMU TPM: https://www.qemu.org/docs/master/specs/tpm.html (swtpm + `tpm-tis-device` on ARM virt).
- QEMU source: `hw/arm/virt-acpi-build.c` (MADT/GICR, GTDT, DBG2, SPCR, IORT, PSCI flag in the FADT) on github.com/qemu/qemu.
- EDK2: `ArmVirtPkg/ArmVirtQemu.dsc` and `OvmfPkg/AcpiPlatformDxe/AcpiPlatformDxe.inf` on github.com/tianocore/edk2.
- Microsoft: https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables (MADT/GIC inbox, GTDT, CSRT/HAL extension and the ACPI device requirement, DBG2, hardware-reduced FADT); https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--dbgsettings ; https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--set ; https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/supported-ethernet-nics-for-network-kernel-debugging-in-windows-10 .
- Repository: `docs/PLAN.md`, ADR 0001/0003/0004/Phase 0 draft, INTERFACES, RISKS, `re/f0-m1-fiq-irq.md`, `tools/acpigen`.

## 10. Proposed next steps (none executed)

1. Wait for A1, A2, A4 and update the marked sections; in particular: GIC version expected by Windows, FADT/parking requirements, entry point and signing of the HAL extension.
2. Task for an executor: extend `acpigen` (`qemu-virt` model, options, GICR, FADT, SSDT) with tests.
3. Task for an executor (after H1/H4): `WatariQemuPkg` + `WatariAcpiOverride` driver.
4. Decide with Marco: where the images live (space) and which tool for the debugger VM.
5. Only after backup confirmation: H3, H5, V0.
