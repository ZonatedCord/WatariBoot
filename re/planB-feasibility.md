# Plan B (EL2 layer with virtual GICv3): feasibility and cost, status as of 2026-10-14

Method: read-only (`gh api`, WebSearch, WebFetch), no clone, no download, no installation. Labels: [verified: source] = read directly; [hypothesis] = my deduction. Abbreviated paths: `m1n1/` = AsahiLinux/m1n1 `src/hv/`; `NTASP/` = NT-for-ASi/m1n1_windows `src/`; `KVM/` = torvalds/linux `arch/arm64/kvm/`.

## 0. Verdict

Plan B is **theoretically possible: YES, high confidence (~85%)**. There is no architectural obstacle; the risks are engineering and time, not feasibility.

The three strongest pieces of evidence:
1. **Windows ARM64 boots on a GICv3 with a virtual CPU interface and an emulated distributor.** In our repo, V1 (QEMU/HVF, GICD + GICR + MSI frame + PSCI) reaches the lock screen [verified: docs/FAILURES.md F-11, F-14 "V1 ... reaches it at 120 s"]. In addition, Windows 11 ARM64 runs as a KVM guest on M1 under Asahi Linux (`-cpu host -enable-kvm`, ramfb) [verified: AsahiLinux/docs `docs/sw/windows-11-vm.md`]: the combination "M1 + hardware vGICv3 + Windows" is already in use, although with ARM virtio and not with real peripherals.
2. **The M1 has the virtual GICv3 CPU interface in hardware, with 8 list registers.** [verified: NTASP/hv_vgic.c lines 51-62: "8 list registers", 32 priority levels, 16-bit IDs, TDIR supported, no GICv4, no legacy; KVM/hyp/vgic-v3-sr.c derives `max_lr_idx` from `ICH_VTR_EL2`, and KVM has been running GICv3 guests on M1 since 5.14.] Only the distributor/redistributor (MMIO) needs to be emulated, which stage-2 allows.
3. **The hypervisor pieces on M1 exist and work**: m1n1 `hv` (stage-2, MMIO trap, multiple vCPUs, guest timer via FIQ, PSCI/smp) and the KVM patch set "Initial host support for the Apple M1" (Zyngier, 2021, in 5.14). And NTASP has already merged them into an (incomplete) `hv_vgic.c`.

Estimate: (a) QEMU TCG prototype with Windows booting on top of our layer: **8-12 weeks** (range 6-16); (b) Windows on a real MacBook up to the lock screen with framebuffer and USB: **a further 14-24 weeks** after (a), a good part of which can be parallelized (USB/PHY driver, hardware bring-up). Realistic total **5-8 months**, one developer with agents [hypothesis, see sec. 9]. Main risks: sec. 9.

## 1. Virtual GIC on M1

- **Virtual CPU interface in hardware.** [verified: NTASP/hv_vgic.c 51-62] Apple "implements the GIC CPU interface registers": 32 priority/preemption levels (5 bits), 16-bit IDs, 3 affinity levels, `ICC_SRE_EL2.SRE` reserved to 1 (no GICv2), TDIR (FEAT_GICv3_TDIR), **8 list registers**, no direct injection (not GICv4, no NMI), no IRQ/FIQ bypass, no extended SPI/PPI range. The NTASP code uses `ICH_LR0..7_EL2`, `ICH_VMCR_EL2`, `ICH_HCR_EL2`, `ICH_ELRSR_EL2` [verified: hv_vgic.c 1578-1700].
- **M1/M2 errata: broken SEIS.** On M1, M1 Pro/Max and M2 the vGIC generates local SErrors when the guest writes the GIC registers incorrectly; KVM has a workaround restricted to these CPUs [verified: commits torvalds/linux d11a327ed9 "Restrict SEIS workaround to known broken systems", cae889302e "List M1 Pro/Max", decb17aeb8 "Add Apple M2"]. NTASP knows about it ("an errata here exists on pre-M3 SoCs that can result in a host SError") [verified: hv_vgic.c 53-55]. Our layer must handle the SErrors from `ICH`/`ICC`, or announce SEIS=0 (KVM: `vgic_hcr |= ICH_HCR_EL2_vSGIEOICount`... the detail must be read in `vgic-v3.c`) [hypothesis on the method].
- **Maintenance interrupt.** The M1 has one maintenance interrupt per CPU, but it is not wired into a maskable controller: it arrives as a FIQ-like event. [verified: lore "Apple M1 KVM", commit 43c5c868bd "vgic: Allow registration of a non-maskable maintenance interrupt", 13aad0c00b "irqchip/apple-aic: Register vgic maintenance interrupt with KVM"; AsahiLinux/linux `irq-apple-aic.c` ~lines 424-445: after the event loop it reads `ICH_MISR_EL2`, if non-zero it calls `AIC_FIQ_HWIRQ(AIC_VGIC_MI)`; if KVM has not handled it, it disables `ICH_HCR_EL2.En`; enum `AIC_VGIC_MI` line 237, mapping line 1108.] In our EL2 layer the MI must be read from `ICH_MISR_EL2` at every exit/IRQ, without depending on a controller.
- **Distributor/redistributor in software.** KVM emulates them with stage-2 MMIO traps. Size: `vgic.c` 1350 lines, `vgic-mmio.c` 1131, `vgic-mmio-v3.c` 1172, `vgic-v3.c` 1055, `vgic-init.c` 845, `vgic-kvm-device.c` 819 (the userspace API part, not needed by us), `vgic-v4.c` 555, `vgic-its.c` 2853 (ITS), `hyp/vgic-v3-sr.c` 1272 [verified: line count via `gh api`, HEAD 2026-10-14]. The core we need (MMIO v3 + LR state + SGI dispatch) is about **4-5 thousand lines in KVM**, but KVM is generic (live migration, nested, GICv2, GICv4). For us, without ITS (Apple PCIe MSIs do not use ITS), the estimate is **2,500-4,000 lines of C** [hypothesis].
- **Windows does not require ITS.** [verified: F-14/F-11: V1 without ITS, with GICD+GICR+MSI frame, reaches the lock screen].

## 2. m1n1 hypervisor (AsahiLinux/m1n1 `src/hv/`)

Lines: `hv.c` 513, `hv_vm.c` 1433, `hv_exc.c` 683, `hv_virtio.c` 308, `hv_vuart.c` 150, `hv_aic.c` 95, `hv_wdt.c` 171, `hv_asm.S` 196, `hv.h` 136: about **3,700 lines** [verified: count via `gh api`].

What it can do:
- **Stage-2**: `hv_vm.c` programs `VTCR_EL2`/`VTTBR_EL2` (16 KB granule, full PA), maps `hv_map_hw` (RAM identity), `hv_map_hw_ro`, `hv_map_sw`, `hv_map_hook` (MMIO trap with C callbacks), `hv_pa_rw` [verified: hv_vm.c 134-142, 309-398, 973]. Identity map ready; 16 KB stage-2 is independent of the granule of Windows' stage-1 (4 KB) [hypothesis, ARM architecture].
- **Multiple vCPUs**: `hv_start_secondary`, `hv_init_secondary` copy HCR/VTCR/VTTBR/... to the secondary cores; `smp_call4`; `hv_started_cpus[]` [verified: hv.c 284-370]. But exception handling is a "slow single-threaded path": only one CPU at a time is "interruptible" for the proxy (`hv_pinned_cpu`/`interruptible_cpu`) [verified: hv_exc.c 599-640].
- **Guest timer**: `hv_update_fiq()` sets `HCR_EL2.VF` when the EL02 physical/virtual timer is `ISTATUS|ENABLE` and handles `VM_TMR_FIQ_ENA_EL2` (`ENA_P`, `ENA_V`); `hv_exc_fiq()` masks `CNTP_CTL`/`CNTV_CTL` with `IMASK` and forwards [verified: hv_exc.c 152-185, 599-640; `SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2` is also in AsahiLinux/linux irq-apple-aic.c line 187]. So m1n1 does NOT implement a vGIC: it injects a virtual FIQ and the guest talks to the AIC directly (Linux/macOS).
- **IPI**: m1n1 uses the Fast IPIs `SYS_IMP_APL_IPI_SR_EL1` and signals them to the guest as a virtual FIQ (`ipi_pending`) [verified: hv_exc.c 171, 649-657].
- **Overhead**: no measurement published in the sources read; the project presents it as a tracing tool, not a production one [hypothesis from the documentation `m1n1-hypervisor.md`, which speaks only of debugging macOS with the proxyclient].
- **Is the proxy indispensable?** In the C code: every unhandled exception (`hv_exc_proxy` -> `uartproxy_run`) hands control to the Python proxy via UART/USB and **the guest stops until the host responds** [verified: hv_exc.c 43-131, `uartproxy_run(&start)` line 71; docs: `run_guest.py` from the Python proxyclient, DFU port, lines 116-130 of m1n1-hypervisor.md]. The MMIO hooks in C run without the proxy, but setup (maps, hooks, guest loading, hv_start) is driven from Python. For "production" use the C must be restructured into an autonomous startup: static setup in C, unexpected exceptions = log + reset instead of proxy. [hypothesis, estimated work 2-3 weeks]. The watchdog (`hv_wdt.c`, 171 lines) is likewise designed for debugging.

## 3. NTASP `m1n1_windows`

- Lines: `hv_vgic.c` **1900**, `hv_psci.c` **1744** [verified: `gh api ... | wc -l`]. License: SPDX `BSD-2-Clause-Patent OR MIT` (vgic), `MIT OR BSD-3-Clause` (psci) [verified: re/ntasp-analysis.md sec. 6]. The hv_vgic.c header says "Inspiration borrowed from the KVM vGIC driver": risk of GPL-2.0 contamination to be checked file by file [verified: header; consequence: hypothesis].
- **State of completeness** (markers in the source) [verified: hv_vgic.c]: ISPENDR/ICPENDR/ISACTIVER/ICACTIVER "not implemented" (lines 487, 527, 565, 602); "bank GICD_IGROUPR0 for cores 0-7 ... since we're booting with 1 core atm, we can ignore" (368, 708); many `TODO: do the AIC operation associated with this` (1082-1224); "we only support the timer interrupt right now" (634, 795); GICD_SGIR and ICC_SGI1R: actions "TODO: actually do the action here" (1005, 1014); ITS mapped but "Disabled for now" (1880-1887). `ENABLE_VGIC_MODULE` is a compile-time macro.
- **Timer injection**: NTASP's `hv_exc.c` calls `hv_vgic3_inject_irq` for the timers when an LR is free, otherwise queues in `timer_queue` (lines 186-239, "TODO: proper injection") [verified]. Priorities read for INTID 17/18: to be checked (EL1 timer PPIs are 27/30 in a standard GIC) [hypothesis: could be an internal mapping].
- **PSCI**: `hv_psci.c` is "a straight port of the implementation in ARM Trusted Firmware-A", with `CPU_ON` (lines 487-488, 1687), power domain trees for T8103/T8112/T600x, but many "TODO"s (SYSTEM_OFF as a reboot placeholder, deep sleep not done) [verified].
- **Does Windows boot on it?** There is no public evidence. Mu's README says "hangs on the handoff between bootloader and kernel"; the site says "barely booting ... only Windows to Go boots tested" (see `re/ntasp-analysis.md` sec. 4) [verified as far as public; in tension with each other].
- **Reuse**: as a **reference for structure and registers** (GICD/GICR offsets, LR layout, PSCI tree) it has value; as code to port it is immature (1 core, pending/active missing, no SMP tried). Better to have a small, tested vGIC of our own, looking at NTASP and KVM as references [hypothesis]. The quality judgment of their UEFI side (`re/ntasp-quality-audit.md`: "to be rewritten") suggests caution here too.

## 4. Timer and FIQ in plan B

- On the M1 the ARM timers (CNTP/CNTV) are wired as FIQs and are not PPIs of a GIC: the AIC exposes them as FIQ events and there is a `VM_TMR_FIQ_ENA_EL2` register for the "guest" (EL02) timers [verified: AsahiLinux/linux irq-apple-aic.c 187-189, 500-526, 580-600: `AIC_TMR_EL02_PHYS/VIRT`, `VM_TMR_FIQ_ENABLE_P/V`].
- **m1n1 (hv_exc.c)**: `hv_update_fiq()` and `hv_exc_fiq()` (see sec. 2) treat the guest timer as a virtual FIQ.
- **KVM on M1**: the physical FIQ is handled as an IRQ while a guest is running ("Handle physical FIQ as an IRQ while running a guest", in the kvm-arm64/m1 branch of 2021) [verified: summary from the lore series "KVM: arm64: Initial host support for the Apple M1" v3/v4, merge 32e92b71b3, 2021-06-06; the patch body was not read]. Generic KVM then presents the timer to the guest as PPI 27/30 of the vGIC.
- **In our layer**: HCR_EL2 sets `IMO|FMO` (KVM does it in `hyp/vgic-v3-sr.c` lines 467-482) [verified]; the timer FIQ arrives at EL2, `CNTP/CNTV_CTL` is read, the timer is masked (`IMASK`), INTID PPI 27 (virtual) or 30 (non-secure physical EL1) is injected into an LR, and it is re-armed when Windows writes the timer (trap on `CNTx_CTL_EL0` or `VM_TMR_FIQ_ENA` as m1n1 does). **Feasible**, at the cost of 1 exit per tick (Windows has a 1-15 ms tick) [hypothesis].
- **PMU FIQ**: handle it like m1n1 (`PMCR0_IMODE_FIQ|IACT`) or disable it, otherwise a FIQ storm [verified: hv_exc.c 642-650; see quality audit D9].

## 5. IPI and SMP

- **Windows -> ICC_SGI1R_EL1**: trap to EL2 (`ICH_HCR_EL2.TALL1`... or TC; KVM uses `vgic_v3_dispatch_sgi` in `vgic-mmio-v3.c` line 1101 [verified]) and routing to the destination vCPU; the destination vCPU is "unblocked" by `kvm_vcpu_kick` (vgic.c lines 452/508/850) [verified].
- **On M1** the physical IPI is AIC: AICv1 has legacy IPIs (`AIC_IPI_SEND 0x2008` with a bit per CPU) and Fast IPIs (system registers `IPI_*`) [verified: irq-apple-aic.c line 88-93, 843; hv_exc.c 649]. Linux on M1 uses IPIs via AIC in the variant `aic1_fipi_info` / `aic1_local_fipi_info` [verified: irq-apple-aic.c 272-316]. In our layer: Windows SGI -> adds the SGI to the pending state of the target vCPU -> sends an AIC IPI (legacy or fast) to the physical CPU -> at EL2 the IPI FIQ/IRQ injects into the LR. 1:1 hypervisor vCPU = physical core, no scheduler. [project hypothesis, supported by the individual verified mechanisms]. m1n1 already does the lower piece (`smp_send_ipi`, hv.c lines 375-401).
- **Safety note**: our own `docs/FAILURES.md` F-15 shows that Windows with the parking protocol goes to 0x5C; with PSCI+vGIC (V1) it was instead seen to boot with `-smp 4`. Plan B uses PSCI (CPU_ON), which is the already-exercised path. [verified: F-15, V1]

## 6. Peripherals in passthrough

- Identity stage-2 for RAM and peripheral MMIO (`hv_map_hw` for all regions, with trapping only on the fake GIC regions and those to be protected) is exactly what m1n1 does for macOS: NVMe/ANS, USB (dwc3 + atcphy), DART, display (dcp/DisplayPort) remain real and the guest drives them [verified: macOS runs under m1n1 hv with real peripherals, docs `m1n1-hypervisor.md`].
- **AIC interrupts -> virtual SPIs.** The AIC is not a GIC: IRQs are events with registers `AIC_EVENT 0x2004`, `MASK_SET/CLR`, `TARGET_CPU` (v1) [verified: quality audit D2-D5]. EL2 layer: enable only the IRQs that Windows wants (`GICD_ISENABLER` trap), physical IRQ -> EL2 (HCR.IMO), reads the event, masks the IRQ in the AIC, injects SPI `32+n` into the LR, and unmasks at EOI. Since `LR.HW=1` requires a physical GIC INTID (which does not exist), **software EOI** is needed: the `EOI` bit of the LR + maintenance interrupt, or a trap on `ICC_EOIR1_EL1` (TDIR/TC) [hypothesis, but it is the way NTASP plans to do it: parameter `hw_status`, `ICH_LR_MAINTENANCE_IRQ` in `hv_vgic3_inject_irq`, hv_vgic.c 1707-1722, verified].
- **DART (IOMMU)**: Windows programs the DARTs with physical addresses = guest addresses = physical (identity): it works. The risk is that a driver makes a mistake and does DMA onto the EL2 layer's memory (no protection because the M1 has DART, not SMMU); mitigation: the layer programs the DARTs to deny its own region, or leaves the `hv`-only DART unmapped [hypothesis]. NTASP has `hv_smmu.c` but I did not read it.
- **Things that would prevent it (none found, but to be verified)**: (i) the EL2 layer's memory must be removed from the memory map given to Windows (UEFI memory map): trivial [hypothesis]; (ii) SPIs have a single priority level and CPU target, hence 1 IRQ at a time per core: ok; (iii) Windows must drive ANS (Apple NVMe): StorPort driver to be written, regardless of plan B (`drivers/`, the work is the same in both plan A and B). Passthrough does not prevent it.
- A peripheral that on M1 uses **PCIe MSI** (Wi-Fi, etc.) requires MSI -> virtual SPI: out of scope for milestone (b).

## 7. Prototype in QEMU

- **HVF with EL2 in the guest: NO on this Mac.** QEMU refuses: "mach-virt: HVF does not support providing Virtualization extensions to the guest CPU" [verified: WebSearch -> Alexander Graf patch "hw/arm/virt: Extend nested and mte checks to hvf" (2021) and 2025 QEMU commit limiting a nested Xen test to TCG: "QEMU only supports accelerating EL0 and EL1"]. Apple exposes nested virtualization only in Virtualization.framework (`isNestedVirtualizationSupported`, only M3 or later, macOS 15) [verified: Parallels forum/hada.io via WebSearch], not in Hypervisor.framework. **Our Mac is an M1: there is no nested in any case.** (The question "M3+ with macOS 15" is confirmed but irrelevant for us.)
- **TCG with `virtualization=on`**: QEMU emulates EL2 and the GICv3 with the virtualization extensions (registers `ICH_LR*`, `ICH_VTR`, maintenance interrupt) [verified: qemu hw/intc/arm_gicv3_cpuif.c (functions `ich_lr_*`); hw/arm/virt.c line 1363 `has-virtualization-extensions`, ARCH_GIC_MAINT_IRQ]. So a layer of ours can run under TCG. Limits: (i) **slow** (Windows 11 ARM64 under TCG: order of tens of minutes to boot, a few hours for the OOBE) [hypothesis]; (ii) QEMU's emulated GIC has a real distributor while the M1 does not: our layer must behave as if it did not see it (ignore the real GICD and use only the CPU interface). A minimal "AIC" model must be made in QEMU to try the AIC IRQ -> SPI path, and this too is new work (see F-06: today there is no AIC in QEMU).
- **Alternative: develop directly on M1 with tethered m1n1** (host = second computer), as NTASP does: fast, but requires the debug host and risks hanging the hardware. A sensible combination: layer written in portable C (EL2), first on TCG, then on M1.
- With HVF, for testing the *software* vGIC only, could the layer be run as an "EL1 guest" without EL2? **No**: EL2 is needed for the traps [strong hypothesis].

## 8. Expected performance

- With a hardware-CPU-interface vGIC, the overhead for MMIO exits (GICD/GICR) is paid only in the few configuration accesses; ack/EOI/priority of IRQs happen in hardware without exits [verified for the GICv3 architecture and for the fact that the M1 implements the ICV_* registers; KVM exploits this]. On ARM the dominant costs are interrupt injection and traps, not stage-2 [verified: Dall et al., "ARM Virtualization: Performance and Architectural Implications", ISCA 2016, https://www.cs.columbia.edu/~nieh/pubs/isca2016_armvirt.pdf: overhead for real workloads comparable to x86, and dominated by the hypervisor's software design, not by transition speed].
- Our case is better than generic KVM: no scheduler, 1 vCPU = 1 core, no device emulation, identity stage-2 with large block maps (low TLB footprint; m1n1's 16 KB granule, maps up to 32 MB/1 GB, is good) [hypothesis]. For pure CPU workloads and GPU/disk passthrough the expected overhead is **under 3%**, our estimated "1%" in PLAN.md is plausible but not measured [hypothesis]. The real cost is on interrupts: each peripheral IRQ = physical IRQ at EL2 + injection + software EOI (2-3 exits, order 1-3 microseconds each) [hypothesis; to be measured], relevant only at high IRQ rates (NVMe at full speed, network).
- Stage-2 must be activated with TLB tags (VMID) and without useless DSBs; avoid m1n1's "slow single-threaded path" [verified for m1n1's behaviour: hv_exc.c 599-640].

## 9. Verdict, estimate and risks

**Possible: YES, confidence ~85%.** I found no obstacle of principle. What is not proven: Windows using the M1's *real* peripherals (ANS, USB, DART) on top of the hypervisor: nobody has published it (NTASP has been trying for more than a year without a documented boot).

Work estimate (one developer + Sonnet agents, realistic calendar hours, not CPU) [hypothesis]:

| Part | Weeks |
|---|---|
| Detailed design + ADR (morasca-agent), choice of m1n1 reuse vs our own writing | 1 |
| EL2 skeleton: entry from UEFI, identity stage-2, sysreg traps, exceptions, serial log (reuse m1n1 hv_vm/hv_exc where possible) | 2-3 |
| vGICv3: GICD/GICR MMIO, LR, pending/active, priorities, SGI via `ICC_SGI1R`, timer PPI, MI; unit tests on QEMU | 3-4 |
| PSCI (CPU_ON, SYSTEM_RESET/OFF, VERSION) + SMP 4/8 cores | 1 |
| Integration with firmware/ACPI (MADT/GTDT with fake GICD, FADT with PSCI via HVC) and Windows boot in TCG | 1-2 |
| **(a) Total QEMU TCG prototype with Windows** | **8-12 (6-16)** |
| Porting to real M1: autonomous startup without proxy, watchdog, SError/SEIS, AIC -> SPI, AIC IPI | 4-6 |
| Windows: storage (USB mass storage with standard XHCI after PHY bring-up; ANS NVMe is a new driver, 6-10 weeks by itself) | 4-10 |
| Display from iBoot framebuffer/GOP; USB keyboard; internal SPI-HID keyboard optional | 2-4 |
| Hardware debugging (hangs, DART, cache, errata) | 4-8 |
| **(b) Addition up to the lock screen** | **14-24** |

Recommended minimal path to reduce (b): boot from **USB-C** with keyboard and USB disk (standard XHCI) and **without** native ANS, as NTASP does ("Windows to Go") [verified: re/ntasp-analysis.md sec. 4, 5]; ANS and SPI after the milestone.

Main risks, in order:
1. **M1 SEIS/SError errata on the vGIC** (unhandled traps = hypervisor panic) [verified: KVM commits d11a327ed9, cae889302e]. High, but KVM shows the way.
2. **Fidelity of the vGIC towards Windows**: Windows checks GICD_TYPER, IIDR, GICR_TYPER, `GICD_CTLR.RWP` etc.; one wrong register = hang without bugcheck (like F-14). Mitigated by comparisons against QEMU GICv3 on the V1 bring-up (which works).
3. **The m1n1 hv code is designed for debugging** (proxy, single-thread, watchdog): making the C autonomous is non-trivial work and without automated tests [verified sec. 2].
4. **AIC interrupt -> SPI without `LR.HW`**: software EOI and non-maskable maintenance interrupt, risk of storms/starvation if wrong [verified sec. 1; the rest is hypothesis].
5. **Windows drivers (ANS, DART/PEP, SPI-HID)**: plan B does not eliminate them; the cost is the same as plan A. Driver signing for distribution (PLAN.md).
6. **Project constraint**: Watari Boot's CLAUDE.md says "no hypervisor ... unless explicit user decision": Marco's decision is needed before starting; plan B is also, in practice, NTASP's road (hobby project, 1 active person), so there is a possible duplication of effort and discoveries.
7. **Licenses**: code derived from KVM = GPL-2.0; m1n1 is MIT; mixing requires attention to headers (project CLAUDE.md).
8. **Debug host**: for the bring-up on M1 a second computer (DFU + proxy) or a serial line is needed; the plan does not mention it except for Phase 2.

Suggested next step (not done): `morasca-agent` design session -> `docs/design/planB/` and a 1-week spike: minimal EL2 layer in QEMU TCG that activates stage-2 + ICH_* and injects a single timer PPI into a Linux guest (proof that the path works before tackling Windows).

## 10. Sources
- Repos: NT-for-ASi/m1n1_windows (`src/hv_vgic.c`, `hv_psci.c`, `hv_exc.c`, `Makefile`); AsahiLinux/m1n1 (`src/hv/*`); AsahiLinux/linux branch `asahi` (`drivers/irqchip/irq-apple-aic.c`); torvalds/linux (`arch/arm64/kvm/vgic/*`, `hyp/vgic-v3-sr.c`, commits d11a327ed9, cae889302e, decb17aeb8, 43c5c868bd, 13aad0c00b, e4f7417e96, 32e92b71b3, 08f4f41c1e); qemu/qemu (`hw/intc/arm_gicv3_cpuif.c`, `hw/arm/virt.c`); AsahiLinux/docs (`docs/sw/m1n1-hypervisor.md`, `windows-11-vm.md`).
- Web: https://lore.kernel.org/kvm/87cztgvyns.wl-maz@kernel.org/T/ (series "Initial host support for the Apple M1", the direct page answered 403, I used the summaries from search results); https://lkml.iu.edu/hypermail/linux/kernel/2301.0/01203.html ("Allow registration of a non-maskable maintenance interrupt"); https://patchew.org/QEMU/20211123122859.22452-1-agraf@csgraf.de/ ; https://qemu.googlesource.com/qemu/+/72846594b089f03bed1bb46958c910afcb859d49 ; https://forum.parallels.com/threads/macos-15-sequoia-nested-virtualization-for-m3-macs.364397/ ; https://www.cs.columbia.edu/~nieh/pubs/isca2016_armvirt.pdf .
- Limits: I did not read the body of the KVM patches "Handle physical FIQ as an IRQ" and "Be tolerant to the lack of maintenance interrupt" (only titles and summary); I did not read NTASP's `hv_smmu.c` or `hv_vtpm.c`; the line count of `hv_vgic.c` includes comments and dead code; no performance measurement was made, all numbers in sec. 8 are estimates.
