# Credits

Watari Boot stands on the work of many people and projects. This page thanks them and says, for each one, how it helped us and whether we used its code or only studied it.

**We do not copy code from GPL projects (Linux, m1n1, KVM); they were studied as references. Any mistakes are ours.**

Legend: **code used** = the project's code or binaries are built, run or applied by this repository; **reference only** = we read it (documentation, source, issues) to learn facts, and wrote our own text and code.

## Projects we build on

| Project | How it helped us | Use |
|---|---|---|
| [TianoCore EDK2](https://github.com/tianocore/edk2) | UEFI firmware base. We build `ArmVirtPkg/ArmVirtQemu` and apply a small patch (`tools/edk2-hvf-gicv3.patch`). BSD-2-Clause-Patent. | code used |
| [QEMU](https://github.com/qemu/qemu) | Our test bench: `virt` machine, ACPI table generation (`hw/arm/virt-acpi-build.c` read as reference), TCG/HVF. | code used (as a tool) |
| LLVM / clang / lld | Compiler and linker for the host tools and unit tests. | code used (as a tool) |
| ACPICA `iasl` | ASL compiler and ACPI table disassembly. | code used (as a tool) |

## Hardware knowledge

| Project | How it helped us | Use |
|---|---|---|
| [Asahi Linux](https://github.com/AsahiLinux) | The source of truth about Apple Silicon hardware. Thank you to Hector Martin, Sven Peter, Marc Zyngier (KVM on Apple M1) and all Asahi Linux contributors. | reference only |
| [m1n1](https://github.com/AsahiLinux/m1n1) (Asahi Linux) | Boot stage and hypervisor; `src/aic.c` and `aic_regs.h` explained the AIC registers and the FIQ/PMC behavior. GPL-2.0-or-later since 2.0.0. | reference only |
| Linux drivers (`irq-apple-aic`, `apple_wdt`, others) | Register layouts and sequences for the interrupt controller and watchdog. GPL-2.0. | reference only |
| t8103 device trees (`t8103.dtsi`, `t8103-j293.dts`) | Addresses, IRQ numbers and CPU topology, used to check every value we generate. | reference only |
| Asahi Linux documentation (`AsahiLinux/docs`) | Boot flow, hardware notes, platform description. | reference only |
| KVM on Apple M1 (Marc Zyngier's series, [lore.kernel.org](https://lore.kernel.org/kvm/87cztgvyns.wl-maz@kernel.org/T/)) | Showed which M1 quirks matter when running a guest at EL1/EL2. | reference only |

## Prior art and references

| Project | How it helped us | Use |
|---|---|---|
| [NT for Apple Silicon (NTASP)](https://github.com/NT-for-ASi) | The first public effort to run Windows on Apple Silicon, by Arminder Singh (amarioguy) and contributors. Its documentation and code showed what is needed and where the hard parts are; we chose a different, native-first path, and its data (CPU topology, MPIDRs) was checked against the device trees. Thank you for opening the way. | reference only |
| [Wack0/NtHalExts](https://github.com/Wack0/NtHalExts) | Prior work on Windows HAL extensions, which guided our work on the AIC/timer extension. | reference only |
| [ms-iot/imx-iotcore](https://github.com/ms-iot/imx-iotcore), [ms-iot/rpi-iotcore](https://github.com/ms-iot/rpi-iotcore) | Public examples of HAL extensions (timers, DMA) and of a Windows BSP for a board without a GIC. | reference only |
| Windows on Raspberry (WoR): pftf/RPi3, andreiw/RaspberryPiPkg (now worproject/RaspberryPiPkg), raspberrypi/windows-drivers | The Raspberry Pi 3 precedent: how a non-standard SoC boots Windows through UEFI and ACPI, and where Windows still needs built-in support. | reference only |
| [qemu-t8030](https://github.com/TrungNguyen1909/qemu-t8030) by Trung Nguyen | An Apple SoC model for QEMU; we studied its AIC modeling. | reference only |
| [Inferno](https://github.com/ChefKissInc/Inferno) (ChefKissInc) | Continuation of qemu-t8030 with a more complete AIC model. | reference only |
| Apple's open-source XNU | Background on how Apple's own kernel drives the interrupt controller and timer. | reference only |
| Linux `arch/arm64/kernel/acpi_parking_protocol.c` | Showed the mailbox layout of the ACPI parking protocol. | reference only |
| [FWTS](https://github.com/ColinIanKing/fwts) | Its CSRT checker helped us validate our table layout. | reference only |

## Specifications and documentation

- UEFI and ACPI 6.5 specifications (UEFI Forum): table formats and boot services.
- Arm documents: PSCI, SMCCC, GICv3/GICv4 architecture specification, Arm Architecture Reference Manual (Armv8-A).
- Microsoft documentation: [ACPI system description tables for Windows](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables), HAL extensions and the CSRT ([csrt.pdf](https://cdrdv2-public.intel.com/772760/csrt.pdf)), [Multiprocessor Startup for ARM Platforms](https://cdrdv2-public.intel.com/772757/mp-startup-for-arm-platforms.pdf), [bcdedit /dbgsettings](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--dbgsettings) and KDNET documentation.

## Methods and tools

- The **Ralph Wiggum technique** by Geoffrey Huntley, <https://ghuntley.com/ralph/>: the loop we use for repetitive, verifiable tasks (see `ralph/README.md`).
- **Anthropic Claude Code**: used as the development assistant for research, drafting and code.

---

If we have missed a credit or described your work inaccurately, please open an issue and we will fix it.
