# WatariM1Pkg: EDK2 platform for the Apple M1 on top of Watari EL2

Design: `docs/design/watari-edk2-m1/` (README, DESIGN, PLAN, RISKS). Decisions: `docs/adr/draft-edk2-m1-platform.md`. This is the T12a state (PLAN steps E1-E4): the package builds, boots on the QEMU bench under the layer and publishes ACPI tables. The hypercall console (E8, T12b) and its run on the layer-built guest FDT (E10, `el2/boot/run-m1-8.sh`) are described in the last section. The framebuffer driver (E11), the boot manager and self-test (E12) do not exist yet.

## Build

```
tools/build-edk2-m1.sh [--bench] [--model <acpigen model>] [DEBUG|RELEASE]
```

| Configuration | Output (data disk, never in the repo) | Content |
|---|---|---|
| default (production) | `$WATARI_DATA/build-m1/WATARI_M1_EFI.fd` | no UART of any kind; the console is the Watari EL2 hypercall (`WatariHvcSerialPortLib`, E8); ACPI tables of acpigen model `j293` |
| `--bench` (`-D WATARI_M1_BENCH_PL011=TRUE`) | `$WATARI_DATA/build-m1/WATARI_M1_EFI-bench.fd` | console and debug through the PL011 of QEMU's device tree, acpigen model `qemu-virt` (GIC at QEMU's addresses), Shell with `acpiview`, boots the Shell by default |

Separate EDK2 workspaces (`/tmp/watari-data/build-m1`, `build-m1-bench`); the EDK2 tree gets only the idempotent HVF/GICv3 patch (`tools/edk2-hvf-gicv3.patch`). Nothing is downloaded. Build time: about 1 minute (DEBUG, 8 cores).

Checks that fail the build (all three were run, see below):
1. `tools/check-dsdt-uids.py`: the `_UID`s of `AcpiTables/Dsdt.asl` equal the GICC UIDs of `acpigen madt -m j293` (0-7), and the same after `iasl -d` of the compiled AML.
2. The list of built modules and libraries (`<workspace>/built-modules.txt`) has no `VirtNorFlash`, `QemuFwCfg`/`FwCfg`, `Virtio`, `PciBus`, `PciHostBridge`, `PciLib`, `NvmExpress`, `QemuRamfb`, `PL031`, `Xhci` or `Usb` module; in a production build also no `PL011` (this covers `FdtPL011*`).
3. The image header: `ARM\x64` at +0x38 and `image_size` (+0x10) equal to the FD size (3 MB), by `xxd`.

## Files

| Path | Role |
|---|---|
| `WatariM1Pkg.dec/.dsc/.fdf` | the platform; **flat DSC** (see Decisions) |
| `Library/WatariM1MemInfoLib` | `ArmVirtMemInfoLib`: the PEI MMU map holds RAM (first `/memory` range) and the FV, no peripheral window; bench: one device page for the UART (`PcdWatariM1BenchUartBase`) |
| `Library/WatariM1PlatformPeiLib` (+ `...Bench.inf`) | `PlatformPeiLib`: FDT HOB and FV HOB only; the bench variant adds the PL011 HOB for ArmVirt's DXE serial/debug libraries |
| `Library/WatariHvcSerialPortLib` | `SerialPortLib` over `WEL2_CONSOLE_WRITE` (E8): default console of the production build (see the last section) |
| `Drivers/WatariM1AcpiDxe` | installs FADT, MADT, GTDT, PPTT (built with `tools/acpigen`, model chosen at build time) and the DSDT through `EFI_ACPI_TABLE_PROTOCOL`; lists what is installed on the debug log |
| `AcpiTables/Dsdt.asl`, `Dsdt.inf` | DSDT v0: eight `ACPI0007` processors, `_UID` 0-7; compiled by the EDK2 build with the installed `iasl`, stored in the FV as a raw section named `gWatariM1DsdtFileGuid` |

Other E2 items are settings: variables RAM-backed (`PcdEmuVariableNvModeEnable = TRUE`, `VariableRuntimeDxe`, no FTW/NOR), `VirtualRealTimeClockLib` (the clock starts at the build time, `SOURCE_DATE_EPOCH` = the commit time), reset/shutdown through `ArmPsciResetSystemLib` + `ArmVirtMonitorLib` (PSCI method from the FDT).

## Decisions and deviations from the PLAN

- **Flat DSC instead of `!include ArmVirtPkg/ArmVirt.dsc.inc`** (PLAN E1 allowed it). Decided by reading, not by building both: `ArmVirt.dsc.inc` sets `ENABLE_VIRTIO/SCSI/PCI` itself from `PLAT_XEN` only (no override hook), keeps `HighMemDxe` under `ENABLE_VIRTIO`, and `[Components]` cannot be subtracted. The structure is `ArmVirtQemuKernel.dsc` (header, relocatable PrePi) + `ArmVirtXen.dsc` (the existing ArmVirt platform without fw_cfg and PCI). [verified: source]
- **`AcpiTableDxe` is built without `PlatformHasAcpiLib`.** That library makes it depend on `gEdkiiPlatformHasAcpiGuid`; DESIGN 9.1 had our ACPI driver install it, but our driver depends on `AcpiTableDxe`, so the two would wait for each other. This platform always has ACPI, so the dependency is dropped and the GUID is not installed (the only consumer in ArmPkg, MdeModulePkg, EmbeddedPkg, ShellPkg, FatPkg, SecurityPkg is `DtPlatformDxe`, which this platform does not build). [verified: grep]
- **DSDT FFS rule:** an own `WATARIDSDT` rule (`RAW ASL |.aml` only), because ArmVirt's `ACPITABLE` rule also requires a `.acpi` file (F-33).
- **Image header and FD size** are those of `ArmVirtQemuKernel` (3 MB, `text_offset` 512 KB); compressed `FVMAIN` takes 25 % of `FVMAIN_COMPACT`.
- **Shell and `acpiview`** are in the bench build (acpiview via `-D WATARI_M1_ACPIVIEW=TRUE`); the production build has the Shell too (`BUILD_SHELL`) but without acpiview and not as the default boot option.

## Tests run on the bench (T12a, 2026-11-13) [verified: run output]

`el2/boot/run-m1-7.sh` (needs `tools/build-edk2-m1.sh --bench` once) is `run-m1-4.sh` with this firmware at `0x40280000`, the layer unchanged. Result: PASS in `CPUIF=hw`, `trap` and `trap-notc`: the layer's M1-4 lines, `Found GIC v3 Distributor @ 0x8000000`, 100 timer interrupts injected, `ArmVirtGetMemoryMap: System RAM: PhysicalBase 0x40200000` (our MemInfoLib), `[Bds]Exit the waiting!`, `[Bds]Booting UEFI Shell (default)`, `Shell>`; no `ASSERT`, no exception.

ACPI (E4), `SHELL_CMDS="acpiview -l" ./run-m1-7.sh`:

```
Installed Table(s):
	   1. RSDP   2. XSDT   3. FACP   4. DSDT   5. APIC   6. GTDT   7. PPTT
```

`acpiview -s FACP` shows the 32-bit `DSDT` field and `X_DSDT` (offset 0x8C) = `0xBF573918`, non-zero and equal; the driver's own log lists `FACP APIC GTDT PPTT` in the XSDT and `DSDT (via X_DSDT)`, length 273, revision 2. `iasl -d` of the built AML gives the same eight devices (build check 1).

## Limits (PLAN, "what the bench can and cannot show")

- Bench only: QEMU's device tree (several nodes this firmware ignores), PL011 console, no GOP, ACPI model `qemu-virt` (4 CPUs in the MADT against 8 processors in the DSDT: harmless for listing, the build check always compares with `j293`), FADT with `ARM_BOOT_ARCH = 0` (the `qemu-virt` model has no FADT config).
- Not shown before hardware: 36-bit IPA and the real j293 ranges, GIC windows at `0xF_...`, the FIQ timer path, `ID_AA64PFR0_EL1.GIC` = 0 with `TID3` (the local EDK2 patch is the second safeguard), 4 KB stage 1 on Apple cores, the panel, caches (TCG has none).
- **Not verified:** a RELEASE build (`DebugLib` null path); the production image was built but not booted (it has no console; the bench build is the same code plus the UART); the Windows ACPI behaviour with this DSDT (PLAN E13); `-m 6G` / RAM above 4 GB (E10).
- Suspicion outside this package, **not tested**: `edk2-apple/WatariQemuPkg/WatariAcpiOverride/WatariAcpiOverride.inf` does not list `j293_model.c`, but `qemu_virt_model.c` (which it does list) references `j293_model` in `acpigen_find_model`, so `tools/build-edk2-watari.sh` probably fails to link until that source is added.

## Hypercall console (E8) and the M1-shaped bench run (E10, T12b) [verified: run output, 2026-11-13]

`Library/WatariHvcSerialPortLib` is the `SerialPortLib` of every non-bench build: `SerialPortWrite` issues `WEL2_CONSOLE_WRITE` (`0xC6000004`) in chunks of at most 160 bytes with the buffer address; read/poll report no data. `DebugLib` = `BaseDebugLibSerialPort` (SEC/PrePi and DXE), `BaseDebugLibNull` for `DXE_RUNTIME_DRIVER` modules (after `SetVirtualAddressMap` a buffer address is not a guest PA). `SerialDxe` + `TerminalDxe` sit on it. The bench PL011 switch (`--bench`) is unchanged.

- **Guarded probe**, once per module (each module has its own copy of the library state, so about one probe per module; the layer logs the first few `PSCI_VERSION` calls): quiet unless CurrentEL = EL1, `ID_AA64PFR0_EL1.EL2` != 0 (an HVC cannot be UNDEFINED), `PSCI_VERSION` returns a version and `WEL2_VERSION` returns the tag `WEL2`. NOT_SUPPORTED later turns the library off.
- **Runtime drivers** have `BaseDebugLibNull`: their `DEBUG` output is dropped and an `ASSERT` in them is a silent no-op.
- **PrePi/SEC:** no PCD, memory service or `.bss` (the state word is initialised to a non-zero value so it lives in `.data`); works with the MMU off (PrePi booted and logged from its first line). Whether `.bss` would have worked is **[hypothesis: not needed]**.
- **Layer absent** (the production FD on plain QEMU virt, `-d int`, 20 s, `virtualization=off` and `=on`, `-smp 1`): no serial output, no Undefined Instruction exception, timer IRQs keep arriving (firmware alive). The two HVC (off) / SMC (on) exceptions seen are not the probe (it stops at the EL2 field / CurrentEL check); I believe they are `ArmTrngLib`'s SMCCC call [hypothesis: not traced].
- **`el2/boot/run-m1-8.sh`**: the production FD under the layer with `wel2.abi=fdt wel2.gfdt=build wel2.split=3` (three `/memory` ranges, no UART/PCI/fw_cfg/virtio node; the whole firmware log arrives through the hypercall). Result: PASS in `hw`, `trap`, `trap-notc` (about 7 s each). Checked: PrePi's `ArmVirtGetMemoryMap` line = the first `/memory` range of the layer's FDT (`0x40200000` + `0x29E00000`), the image lies in it, `HighMemDxe` added exactly the other two ranges (`0x6C000000-0x93FFFFFF`, `0x96000000-0xBFFFFFFF`), `BdsDxe` reached (end state of the production build: `[Bds] Booting Boot Manager Menu.`, nothing to boot, no key), vGIC traps handled, timer injected, no ASSERT/exception. `COMPARE_E3=<run-m1-7 log>` compares the `[Bds]` lines up to `Stop Hotkey Service`: identical (15 lines) in all three modes.
- **R-E4 run** (`HIGH=1`: `-m 6G`, firmware at `0x1_0000_0000`, split=3): PASS. The image's range is listed first (`PcdSystemMemoryBase` = `0xC2000000`, size `0x7E000000`, which contains the image above 4 GB); `HighMemDxe` added `0x40200000-0xBFFFFFFF` and `0x142000000-0x1BFFFFFFF`. So PrePi's high-RAM path is exercised on the bench [verified]; the real M1 map is not.
- **Limits:** GIC at QEMU's addresses (`0x08000000`), IPA size 40 bit; no AIC, no FIQ timer (virtual timer PPI 27); no framebuffer; ACPI tables are the `j293` ones (nothing consumes them here); caches not modelled by TCG; the `Include/` directory of the package holds only a `.gitkeep` (the `.dec` lists it and EDK2 refuses a missing directory; a fresh checkout failed the build before).
