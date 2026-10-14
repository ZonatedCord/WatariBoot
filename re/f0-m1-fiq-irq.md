# F0 / A3: FIQ sources of the t8103 (M1) and IRQ alternatives

Ralph task A3, branch `ralph/f0-gate-a`. Analysis based only on Asahi code and documentation, read in read-only mode with `gh api`. No Microsoft binary or symbol was used.

## 0. Conventions and sources

Every claim is marked:
- **[V: source:lines]** = verified by reading that file (line numbers from the `cat -n` output on the given reference).
- **[H]** = hypothesis or deduction, to be verified.

Reference abbreviations:

| Abbreviation | Repo and reference |
|---|---|
| `L` | AsahiLinux/linux, default branch `asahi`, HEAD `77cb8f24c2381a8abb7272d7bbdec548d6426a8a` |
| `M` | AsahiLinux/m1n1, default branch `main`, HEAD `fbec52a81f2b12a5cf75f83a653d46df0bbc36ce` |
| `D` | AsahiLinux/docs, `main`, HEAD `0d1f8917fa88745d62a4c05d802c4a7298c8616b` (text of asahilinux.org/docs) |
| `L0` | AsahiLinux/linux, commit `76cde26394` (8 April 2021, first commit of `irq-apple-aic.c`) |

Main files read: `L:drivers/irqchip/irq-apple-aic.c`, `L:arch/arm64/include/asm/apple_m1_pmu.h`, `L:drivers/perf/apple_m1_cpu_pmu.c` (IRQ parts only), `L:drivers/clocksource/arm_arch_timer.c` (no Apple handling), `L:arch/arm64/boot/dts/apple/t8103.dtsi`, `L:include/dt-bindings/interrupt-controller/apple-aic.h`, `L:Documentation/devicetree/bindings/interrupt-controller/apple,aic.yaml`, `L:drivers/watchdog/apple_wdt.c`, `M:src/aic.c`, `M:src/aic_regs.h`, `M:src/exception.c`, `M:src/smp.c`, `M:src/utils.c`, `M:src/cpu_regs.h`, `M:src/chickens.c`, `M:src/wdt.c`, `M:src/hv/hv_exc.c`, `M:src/hv/hv.c`, `D:docs/hw/soc/aic.md`, `D:docs/hw/soc/wdt.md`, `D:docs/hw/cpu/system-registers.md`.

Context of the t8103 in the Linux DT:
- AIC compatible: `"apple,t8103-aic","apple,aic"` **[V: L:t8103.dtsi:1029-1030]**.
- In the driver, `apple,t8103-aic` maps to `aic1_local_fipi_info` (AIC v1 with local Fast IPIs) **[V: L:irq-apple-aic.c:281-289,309-313]**.
- The `timer` node and the PMU nodes are all attached to `AIC_FIQ`, not to `AIC_IRQ` **[V: L:t8103.dtsi:368-388]**.
- The binding confirms that "this device also represents the FIQ interrupt sources ... which do not go through a discrete interrupt controller" **[V: L:apple,aic.yaml, description]**. The Asahi document says the same: the system timer "bypasses AIC. It is wired straight to FIQ" **[V: D:aic.md, Timer section]**.

---

## 1. List of FIQ sources of the t8103

Linux's FIQ handler (`aic_handle_fiq`) admits that it has no global status register. It checks the sources one by one, because "not dealing with any of these results in a FIQ storm" **[V: L:irq-apple-aic.c:557-572]**. The complete list of sources that Linux checks is therefore the following.

| # | Source | How Linux recognizes it (`aic_handle_fiq`) | Registers that control it | Maskable at the source? |
|---|---|---|---|---|
| 1 | **Fast IPI** (only if `use_fast_ipi`, true on the t8103) | `IPI_SR_EL1 & IPI_SR_PENDING` **[V: L:574-576]** | `SYS_IMP_APL_IPI_RR_LOCAL_EL1` s3_5_c15_c0_0 and `..._GLOBAL_EL1` s3_5_c15_c0_1 to send; `IPI_SR_EL1` s3_5_c15_c1_1 (bit 0 PENDING, write 1 to clear); `IPI_CR_EL1` s3_5_c15_c3_1 (deferred countdown) **[V: L:171-192]** | **Yes.** Only if nobody sends it: no writes to `IPI_RR_*`. The pending state is cleared with `IPI_SR_EL1 = PENDING` **[V: L:aic_init_cpu 864-865]**. **There is no mask bit** in the sources read [H]. |
| 2 | **EL0 physical timer** (`CNTP_*_EL0`, called "HV phys", hwirq 0) | `TIMER_FIRING(cntp_ctl_el0)` = ENABLE=1, IMASK=0, ISTATUS=1 **[V: L:552-555,578-580]** | `CNTP_CTL_EL0` (ENABLE, IMASK, ISTATUS), `CNTP_CVAL/TVAL_EL0` | **Yes, architectural:** `CNTP_CTL_EL0.IMASK=1` (or ENABLE=0). Linux does this on every CPU in `aic_init_cpu` **[V: L:868]**. m1n1 writes 7 (ENABLE\|IMASK\|ISTATUS) in `exception_initialize` **[V: M:exception.c:134]** |
| 3 | **EL0 virtual timer** (`CNTV_*_EL0`, "HV virt", hwirq 1) | `TIMER_FIRING(cntv_ctl_el0)` **[V: L:582-584]** | `CNTV_CTL_EL0` etc. | **Yes**, `CNTV_CTL_EL0.IMASK=1` **[V: L:869; M:exception.c:135]** |
| 4 | **"Guest" physical timer** (`CNTP_CTL_EL02`, hwirq 2; exists only with the kernel at EL2/VHE) | `VM_TMR_FIQ_ENA_EL2.P` and `TIMER_FIRING(CNTP_CTL_EL02)` **[V: L:586-592]** | `SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2` s3_5_c15_c1_3 (bit 0 = V, bit 1 = P) **[V: L:187-189]**; `CNTP_CTL_EL02` | **Yes, with two levers:** the IMP-DEF enable bit (it is "the only real mask" according to the Linux comment: "Only the guest timers have real mask bits" **[V: L:502]**) and the architectural `IMASK` |
| 5 | **"Guest" virtual timer** (`CNTV_CTL_EL02`, hwirq 3) | as above, bit V **[V: L:594-597]** | as above | **Yes**, as for item 4 |
| 6 | **Core PMU** (E-core and P-core) | `(PMCR0 & (IMODE\|IACT)) == (IMODE_FIQ\|IACT)` **[V: L:600-603]** | `SYS_IMP_APL_PMCR0_EL1` s3_1_c15_c0_0: IMODE [10:8] (0 off, 1 PMI, 2 AIC, 3 HALT, 4 FIQ), IACT bit 11, PMI enable [19:12] and [45:44] **[V: L:apple_m1_pmu.h:22-37]** | **Yes:** `PMCR0.IMODE=OFF` and IACT=0, as in `aic_init_cpu` **[V: L:882-883]**. Linux sets IMODE_FIQ only when it enables the PMU **[V: L:apple_m1_cpu_pmu.c:538]** |
| 7 | **Uncore PMC** | `UPMCR0.IMODE==FIQ` and `UPMSR.IACT` **[V: L:605-607]**; the handler does not service it, it only turns it off ("Uncore PMC FIQ fired. Masking.") **[V: L:609-611]** | `SYS_IMP_APL_UPMCR0_EL1` s3_7_c15_c0_4 (IMODE [18:16] on T8020/M1: 0 off, 2 AIC, 3 HALT, 4 FIQ) and `UPMSR_EL1` s3_7_c15_c6_4 (bit 0 IACT) **[V: L:195-204]** | **Yes:** `UPMCR0.IMODE=OFF` **[V: L:886-889]**. Note: on A11 the position of the IMODE field changes (T8015: [10:8]) **[V: M:cpu_regs.h:609-611]** |
| 8 | vGIC maintenance IRQ | Not a physical FIQ: it is a "fake FIQ" (`AIC_VGIC_MI`) generated when KVM is present at EL2 **[V: L:428-441,1102-1115]** | `ICH_HCR_EL2`, `ICH_MISR_EL2` | Irrelevant without a hypervisor; Linux turns it off with `ICH_HCR_EL2.En=0` **[V: L:878]** |

Other notes:
- The number and meaning of the sources are those of the binding: 0 HV phys, 1 HV virt, 2 guest phys, 3 guest virt, 4 PMU E, 5 PMU P **[V: L:apple-aic.h; L:apple,aic.yaml]**.
- In the Linux kernel at **EL1** (non-VHE) the "guest" timers become the normal EL0 timers (hwirq 2/3 remapped to 0/1), while the "HV" ones return `-ENOENT` **[V: L:aic_irq_domain_translate 718-736]**. If Windows runs at EL1 it therefore uses the `CNTP/CNTV_*_EL0` timers; if it made use of EL2 (e.g. Hyper-V/VBS) it would also touch the EL02 timers and the `VM_TMR_FIQ_ENA_EL2` register [H].
- PMU E and PMU P are two distinct FIQs but Linux merges them into one **[V: L:738-740]**.
- There is no single register that says "which FIQ is active" **[V: L:559-562, comment]**.
- The same list of FIQ sources appears in the diagnostic handler `exc_fiq` of m1n1: EL0 timer, EL02 timer, PMCR0, `IPI_SR_EL1`, UPMCR0/UPMSR **[V: M:exception.c:395-461]**. Another, independent implementation arrives at the same list.
- Other **undocumented** fields or registers that might generate FIQs: `UPMSR_EL1` bit 1 ("CTI") **[V: D:system-registers.md, UPMSR section]**. There is no code that handles it. We do not know whether it produces a FIQ [H].

### PMU/IPI registers: discrepancies between sources
- `IPI_RR_*`: Linux defines CPU = [7:0] and cluster = [23:16] **[V: L:173-175]**. The Asahi documentation writes CPU = [3:0] and cluster = [20:16] **[V: D:system-registers.md, IPI section]**. The type [29:28] is consistent (0 immediate, 1 retract, 2 deferred, 3 nowake). For the t8103 Linux applies (it is working code), but our code must use Linux's exact access [H].
- m1n1 and Linux have different numbering of the `UPMCR0` fields for A11, identical for M1 **[V: M:cpu_regs.h:602-619]**.

---

## 2. Can the architectural timer be routed to IRQ?

**Honest answer: in the Asahi sources there is no documented way to make it arrive as an IRQ.** Evidence:

1. The DT maps the timers only as `AIC_FIQ` **[V: L:t8103.dtsi:368-376]**; the driver treats them as "hard-wired" FIQs that do not go through the AIC **[V: L:apple,aic.yaml; D:aic.md]**.
2. The AIC documentation says: "The system timer is the standard ARM64 MSR stuff, and bypasses AIC. It is wired straight to FIQ." **[V: D:docs/hw/soc/aic.md, end of file]**.
3. `arm_arch_timer.c` has no Apple-specific handling. Searching for "apple" and "fiq": no matches **[V: L:drivers/clocksource/arm_arch_timer.c, grep]**. The timer is used as is, and all the dirty work is in the irqchip.
4. Linux does not do any "vtimer through FIQ and IMP-DEF masking" of the remap kind. What it actually does:
   - For the EL0 timers in the FIQ path it uses `irq_ack = aic_fiq_set_mask` and `irq_eoi = aic_fiq_eoi`; but `aic_fiq_set_mask` acts **only on the guest timers** (EL02) through `VM_TMR_FIQ_ENA_EL2`; in the other cases it is a no-op **[V: L:500-531, 620-627]**.
   - As the "ack" of the EL0 timers, architectural masking is needed: the generic `arch_timer_handler` sets `IMASK` in its own handler (generic code of the ARM driver). So the FIQ level drops because the source (ISTATUS+!IMASK) stops asserting [H, deduction from the comment "We mask to ack (where we can)" **[V: L:547]**].
5. Possible leads, all **unverified**:
   - **(a)** `ACC_OVRD_CONVERT_SIQ_TO_IRQ` (bit 22 of `SYS_IMP_APL_ACC_OVRD` s3_5_c15_c6_0) is defined in `M:cpu_regs.h:563` but is **never used** in any repo of the AsahiLinux organization (search for `CONVERT_SIQ_TO_IRQ` in the organization: a single result, the definition). The name suggests a "SIQ → IRQ" conversion, but we do not know what "SIQ" is nor whether it concerns the timer or the FIQs [H, research lead to be tested on hardware, risky].
   - **(b)** `CYC_OVRD` bits [21:20] "FIQ mode (2=disable)" and [23:22] "IRQ mode (2=disable)" **[V: D:system-registers.md; M:cpu_regs.h:530-536]**. m1n1 uses them only in `cpu_sleep()` (non-deep sleep) so that a CPU in WFI is not woken by IRQ/FIQ **[V: M:utils.c:267-271]**. There is no evidence that they remap FIQ to IRQ: they look like controls over **wake-up from WFI**, not over the signal type [H].
   - **(c)** `PMCR0.IMODE`/`UPMCR0.IMODE` value 2 = "AIC" **[V: D:system-registers.md; L:apple_m1_pmu.h:27; L:195-200]**: for the PMU there is a mode in which the interrupt goes to the AIC (so as an IRQ). Linux does not use it and the t8103 DT does not say which AIC hwirq it would be. It applies only to the PMU, not to the timer [H].
   - **(d)** An EL2 shell (like m1n1's `hv_update_fiq`) can turn FIQs into virtual FIQs/IRQs with `HCR_EL2.VF` **[V: M:hv_exc.c:152-180]**. By definition this is a hypervisor, hence contrary to the "no hypervisor" constraint. To be kept only as plan B [H].
6. Operational conclusion: **the architectural timer (CNTP/CNTV) must be considered FIQ-only.** To deliver a tick to Windows as an IRQ, one must avoid enabling it (IMASK=1 / ENABLE=0) and give Windows another tick source (section 3), or discover the lead (a) experimentally.

Note: reading `CNTPCT_EL0`/`CNTVCT_EL0` (the counter) does not generate interrupts and is not affected by masking; there will always be a 24 MHz physical counter [H, common ARMv8; the AIC also exposes a copy of the counter at 0x8020/0x8028 **[V: D:aic.md]**, but this range exceeds the 0x8000 window of the DT node, so it must be verified before use].

---

## 3. Alternative MMIO timers that generate AIC IRQs

### 3.1 What is in the t8103 DT
I listed all the `compatible` strings of `t8103.dtsi` **[V: L:t8103.dtsi, full list via grep]**. The only blocks with "timer" or timekeeping are:
- `arm,armv8-timer`: FIQ (section 2).
- **`apple,t8103-wdt`**: watchdog with interrupt `AIC_IRQ 338`, level high, clock `clkref` = 24 MHz **[V: L:t8103.dtsi:1223-1229, 390-396]**.
- `apple,t8103-smc` with sub-node `smc-rtc`: RTC via SMC (mailbox/RTKit protocol), not a low-latency programmable timer [H; not analyzed].
- `apple,t8103-fpwm`, `apple,t8103-nco`: PWM and clock generator, not timers with IRQs **[V: compatible only]**.

In the Asahi DT for t8103 I found no other "system" MMIO timer (no `arm,armv7-timer-mem`, no generic timer) **[V: list of compatibles]**. The absence from the DT does not prove that the hardware has none: the DT contains only what Linux uses [H].

### 3.2 The watchdog (WDT) as a candidate
Registers (MMIO base 0x23d2b0000, size 0x4000) **[V: L:apple_wdt.c:19-52]**:

| Offset | Register |
|---|---|
| 0x00 / 0x04 / 0x08 / 0x0c | WD0: CUR_TIME / BITE_TIME / BARK_TIME / CTRL |
| 0x10 / 0x14 / 0x1c | WD1: CUR_TIME / BITE_TIME / CTRL |
| 0x20 / 0x24 / 0x2c | WD2: CUR_TIME / BITE_TIME / CTRL |

- CTRL: bit 0 IRQ_EN, bit 1 IRQ_STATUS (write 1 to clear), bit 2 RESET_EN **[V: apple_wdt.c:50-52]**.
- CUR_TIME increments at every tick of clkref (24 MHz) and is writable **[V: apple_wdt.c:27-28]**.
- When `RESET_EN` is active and `CUR >= BITE`, the machine resets; WD1 and WD2 do a normal reset, **WD0 a reset into recovery mode** **[V: apple_wdt.c:22-26; D:wdt.md]**.
- The driver comment states: "WD0 additionally supports a configurable interrupt" and that with IRQ_EN and `CUR >= BARK` an interrupt fires, cleared by writing 1 to IRQ_STATUS **[V: apple_wdt.c:22-35]**. However, the comment cites WD1 names (`APPLE_WDTx_WD1_CUR_TIME`/`BARK_TIME`) while the BARK register exists only for WD0 (offset 0x08): probably a typo in the comment [H].
- The DT declares a single AIC IRQ (**338**) for the whole block **[V: t8103.dtsi:1228]**.
- The Linux driver **never requests this IRQ** (no `irq` in probe, only start/stop/ping/restart) **[V: apple_wdt.c:56-139, grep of `irq|bark|WD0` in probe]**. So the "bark" functionality has never been exercised in Linux; only the comment describes it. [H: it works, but there is no evidence]
- Initial state: "The initial macho (usually m1n1) is booted with the watchdog timer enabled", initial count 120 s; the counters are 32-bit and "wrap", so the maximum timeout is a little under three minutes **[V: D:wdt.md]**. m1n1 disables it by writing 0 to `+0x1c` (WD1) at startup and, for `wdt-version` 2 or 3 (M2 and later), also the secondary one **[V: M:wdt.c:14-48]**. On the t8103 I do not see m1n1 touching WD0/WD2 [H: initial state of WD0 and WD2 unknown].

### 3.3 Evaluation as a system timer for Windows (via the timer HAL extension)
Pros:
- It generates a true **AIC IRQ** (hwirq 338, level), with affinity programmable via `TARGET_CPU` **[V: L:aic_irq_set_affinity 445-463]**.
- 24 MHz clock (resolution ~41.7 ns), writable 32-bit counter **[V]**.
- The block has **three** comparators (WD0/1/2), but only WD0 has the BARK with an interrupt (according to the comment) **[V/H]**.

Cons and risks:
1. The interrupt is documented only by a comment and never used in Linux. We do not know whether it is always enabled in hardware or whether something needs to be unlocked [H].
2. The comparison is `CUR >= BARK`, not modular. The 32-bit counter wraps in about 179 s. To program a tick one must clear `CUR_TIME` and set `BARK`; each reprogramming is a couple of MMIO writes that are not ordered with respect to the counter (the MMIO latency is not quantified in the sources) [H].
3. With WD0, if `RESET_EN` is active and `BITE` expires, the machine reboots **into recovery mode** **[V]**. One must guarantee `RESET_EN=0` on WD0 and set `BITE` to the maximum, otherwise a bug in the HAL reboots the Mac. Dangerous, to be tried only with a backup and under supervision.
4. It is **one** timer for the whole SoC, not one per CPU. Windows ARM64 normally expects one timer per processor [H]; it would need to be multiplexed with IPIs, which depends on section 4.
5. The `wdt` block sits in an always-on domain (nearby "pmgr_mini") and survives partial resets, so it is shared with m1n1/macOS/recovery: changing its settings persistently is risky for hardware safety (see project constraint) [H].
6. Windows also requires a monotonic performance counter (QPC). That counter remains `CNTPCT_EL0`, which is read-only and does not generate FIQ if the timer is masked [H].

Conclusion of section 3: **the WDT is the only candidate visible in the DT** for a tick via IRQ, but it is a **fallback, unverified** candidate. Before relying on it, a hardware experiment is needed (with confirmed backup): program WD0 with RESET_EN=0, BARK at a few ms, and see whether AIC IRQ 338 arrives. The hardware's ADTs (not consulted) might list other timers [H].

### 3.4 Another lever: the AIC's SW_SET
The AIC allows software to assert any hwirq: "Software triggering (ORed with hw line)" **[V: L:irq-apple-aic.c:18; D:aic.md]**; m1n1 exposes `aic_set_sw()` with SW_SET at 0x4000 and SW_CLR at 0x4080 **[V: M:aic.c:192-202; aic_regs.h:12-13]**. It is not a timer, but it makes it possible to create "virtual IPIs" or to defer work as IRQs (section 4) [H].

---

## 4. The AIC's "legacy" IPIs as IRQs

### 4.1 How they work (AIC v1, hence t8103)
MMIO registers ("this CPU" view, AIC base `0x23b100000`) **[V: L:irq-apple-aic.c:76-103; M:aic_regs.h:5-20; D:aic.md]**:
- `0x2000 WHOAMI`, `0x2004 EVENT` (read = acknowledge + reason), `0x2008 IPI_SEND`, `0x200c IPI_ACK`, `0x2024 IPI_MASK_SET`, `0x2028 IPI_MASK_CLR`.
- `IPI_SEND`: bits `0..30` = send the "other" IPI to the CPU with that index; bit 31 = "self" IPI to the current CPU **[V: D:aic.md; L:93-96]**.
- `EVENT` returns type (bits 23:16) and number (15:0): type 4 = IPI, number 1 = OTHER, 2 = SELF **[V: L:79-86]**.
- Explicit per-CPU views: `AIC_CPU_IPI_SET/CLR/MASK_SET/MASK_CLR(cpu)` at `0x5008 + (cpu<<7)` etc.; **defined but never used** by Linux and m1n1 **[V: L:100-103; M:aic_regs.h:17-20]**; the documentation says "TODO Document direct access to per-core state offsets" **[V: D:aic.md]**.

Flow (documented by Asahi) **[V: D:aic.md, "IPI flow"]**:
1. Write the bit to `IPI_SEND`: the ARM IRQ line is asserted.
2. Read `EVENT`: the IPI is masked and the line drops.
3. Write the bit to `IPI_ACK`.
4. Write the bit to `IPI_MASK_CLR` (if `IPI_ACK` had not been given, the line would be re-asserted).

Uses in code:
- **Linux, original version `L0`:** on M1 it used only legacy IPIs. The driver contained `AIC_IPI_SEND`, `ACK`, `MASK_*`, a 32-vIPI software multiplexer and the atomic flags `aic_vipi_flag`/`aic_vipi_enable`; the comment said "Fast IPIs (TODO)" and "sysreg-based IPIs are not supported yet" **[V: L0:irq-apple-aic.c:26-27,82-87,108,158,185-186,595-682]**. So Linux brought up 8-core SMP on the t8103 with legacy IPIs as IRQs, in its first version (April 2021) **[V as code; the fact that it worked on hardware is H, but the driver was accepted upstream with Reviewed-by and Acked-by]**.
- **Current Linux:** with the sysreg fast IPI the legacy path remains "as a fallback" (`use_fast_ipi` disabled for compatibles without fast IPI) **[V: L:commit 2cf6821166 "The legacy IPI code is left as a fallback"; L:834-844, 1043-1044]**. For t8103, however, `aic1_local_fipi_info` is selected, i.e. Fast IPI, so the legacy code is **no longer exercised on t8103** in the current kernel **[V: L:309-313]**.
- **m1n1:** `smp_send_ipi` and the wait loop of the secondary cores use legacy IPIs only if `!cpu_features->fast_ipi` **[V: M:smp.c:115-131, 478-489]**; for M1 `features_m1.fast_ipi = true` **[V: M:chickens.c:76]**. Likewise: the legacy branch is not exercised on t8103 in current m1n1.
- The legacy branch serves SoCs without Fast IPI (before the A11, per the binding description: "Starting from A11, system registers may also be used" **[V: L:apple,aic.yaml]**) and **AICv2 no longer has legacy IPIs at all** **[V: L:commit 2cf6821166, message]** (concerns M1 Pro/Max, not t8103).

### 4.2 Are they enough to send IPIs between 8 cores?
**Yes as a basic mechanism [V as functionality, H as performance].**
- The t8103 has 8 CPUs; `IPI_SEND` has 31 destination bits, so 8 CPUs fit comfortably **[V]**. Linux also requires `AIC_WHOAMI == smp_processor_id()`: the order of the logical CPUs in Linux must match the AIC order **[V: L:900]**. For the t8103 the DT index is E0..E3 = 0..3 and P0..P3 = 4..7 (reg `0x0..0x3` and `0x10100..0x10103`) **[V: L:t8103.dtsi:63-173]**; the AIC index of the P-cores must be verified on hardware [H].
- Limits:
  1. **Only one "other" IPI per CPU** (plus a "self" one, which Linux does not use). There is no sender identity or payload; multiple senders merge into a single event. Linux solves this with a software multiplexer: 32 vIPIs per CPU with shared atomic flags **[V: L:34-35, 850; L0:595-682]**. The Windows HAL would have to do the same (a queue or an atomic mask per CPU) [H].
  2. **Latency:** not quantified in any source I read. Fast IPIs are "even faster" (words of the binding) because they avoid MMIO **[V: L:apple,aic.yaml, description]**, but there is no number [H].
  3. Each IPI costs an `EVENT` round trip (MMIO read with barrier) + ACK + MASK_CLR, i.e. 3 MMIO accesses plus the send **[V: L:aic_handle_ipi 812-836]**.
  4. There is no "deferred/nowake/retract" mode: those exist only in Fast IPIs **[V: L:177-180; D:system-registers.md]**.
  5. **Wake-up from WFI:** on the secondary cores m1n1 does `deep_wfi()` and then, in legacy mode, `aic_ack()` + `IPI_ACK` + `IPI_MASK_CLR` **[V: M:smp.c:118-132]**, so the legacy IPI wakes a core in deep WFI, at least in the code intended for hardware without fast IPI [H on t8103].
- **Alternative with more dimensions:** use AIC software hwirqs (`SW_SET`) with a per-CPU `TARGET_CPU`, one per core, to obtain separate IPIs without a multiplexer [H]; this requires choosing hwirq numbers not connected to real hardware (which ones is not verified).

---

## 5. Is it safe to mask FIQs forever (`PSTATE.F`)?

**Conclusion: yes, provided that all sources are turned off at the source; masking in PSTATE alone is not enough.** Evidence:

1. **No error or watchdog event arrives as a FIQ.**
   - In the FIQ handlers of Linux and m1n1 the only causes are those of section 1: fast IPIs, timers, PMU and uncore PMU. No system error, no watchdog **[V: L:557-613; M:exception.c:395-461]**.
   - The watchdog uses a normal AIC hwirq (`AIC_IRQ 338`), not a FIQ **[V: L:t8103.dtsi:1228]**.
   - Hardware errors go through **SError** (m1n1 enables SError/IRQ/FIQ on the boot core and handles `exc_serr` separately) **[V: M:exception.c:157-158, 463]**. PSTATE.F does not affect SErrors (they are controlled by PSTATE.A) [H, ARM ARM].
2. **Linux itself starts with all FIQs turned off at the source.** `aic_init_cpu` clears pending Fast IPIs, sets `IMASK` on CNTP and CNTV, turns off the guest timers, the vGIC, the PMU (IMODE=OFF, IACT=0) and the uncore, then `isb` **[V: L:859-892]**. Only afterwards does it enable the sources it needs.
3. **m1n1 does the same before masking/unmasking:** "Clear FIQ sources", then `DAIF`: boot core with everything enabled, secondary cores with IRQ and FIQ disabled **[V: M:exception.c:133-160]**.
4. **Risk of a "FIQ storm":** Linux warns that not handling an active source causes a FIQ storm **[V: L:570-571]**. With `PSTATE.F=1` there is no storm, but the line stays asserted: a pending FIQ (even masked) keeps the line high and, according to the ARM rule for WFI, would make the core exit WFI immediately, preventing a real idle [H, ARM ARM, not verified in the Asahi sources]. Therefore the sources must also be **turned off**, as `aic_init_cpu` does, not just masked.
5. **State left by firmware or macOS:** the loader starts from iBoot/m1n1, so the initial state of the sources is not guaranteed. That is why m1n1 and Linux clear them at every boot of every CPU **[V]**. Our EDK2 firmware and/or the HAL extension must replicate **the `aic_init_cpu` sequence for every core** before handing over to Windows [H, recommendation].
6. **What Windows might turn back on:** if Windows uses the architectural timers (CNTP/CNTV) it cannot avoid enabling them. This is the weak point, see section 2. If it uses the Apple PMU (non-standard, so unlikely without a driver) it would turn it back on [H].
7. **A case not to forget:** `PSTATE.F` is not enough if a higher level (EL2) routes FIQs via `HCR_EL2.FMO`: with FMO=1 the FIQ goes to EL2 even if EL1 has masked it [H, ARM ARM]. The firmware that leaves Windows at EL1 must have consistent FMO and IMO.

---

## 6. Conclusion

### 6.1 Table: source → can it be avoided? → IRQ alternative → confidence

| Source | Can it be avoided? | IRQ alternative | Confidence |
|---|---|---|---|
| EL0 physical/virtual timer (`CNTP/CNTV`) | **Yes, if not enabled** (IMASK=1 / ENABLE=0) **[V: L:868-869]**. But Windows will use it as the architectural timer | **None documented.** Leads: watchdog WD0 via AIC IRQ 338 (sec. 3); `ACC_OVRD_CONVERT_SIQ_TO_IRQ` (sec. 2, unknown) | Avoiding: high (V). IRQ alternative: **low** (H) |
| EL02 guest timer (VHE/EL2 only) | Yes: `VM_TMR_FIQ_ENA_EL2=0` **[V: L:874-875]** and do not run at EL2 | None | High for avoiding (V), relevant only if EL2 is used |
| Fast IPI | **Yes**, never write `IPI_RR_*`; clear `IPI_SR_EL1` at startup **[V: L:864-865]** | AIC legacy IPIs (sec. 4) via `IPI_SEND`, received as type 4 IRQs **[V: L:415-418]**; or software hwirq via SW_SET | Avoiding: high (V). Legacy working on t8103: **medium-high** (V as historical code `L0`, not exercised today) |
| Core PMU (E/P) | **Yes**: `PMCR0.IMODE=OFF`, IACT=0 **[V: L:882-883]** | `IMODE=2 (AIC)` exists but which hwirq is not documented; not needed by Windows | Avoiding: high (V). Alternative: low (H) |
| Uncore PMC | **Yes**: `UPMCR0.IMODE=OFF` **[V: L:886-889]** | Same (`IMODE=2`) | High (V) |
| vGIC maintenance | Yes, there is no EL2/KVM | n/a | High |
| Other unknown FIQs (e.g. `UPMSR` "CTI" bit) | Unknown | n/a | **Low** (H) |
| Errors and watchdog | They are not FIQs: SError / AIC IRQ 338 **[V]** | n/a | High |

### 6.2 The three main risks for the "no FIQ" strategy

1. **The system tick.** Windows ARM64 normally uses the architectural timer, which here is FIQ-only. The sources contain no documented way to route it as an IRQ. The only visible alternative is the watchdog (IRQ 338), never exercised, with a single useful comparator, 32 bits that wrap in about 179 s, and the risk of rebooting the Mac into recovery if the `RESET_EN` bit of WD0 is set wrongly. If the timer HAL cannot be replaced, the "never FIQ" strategy falls apart. Mitigation: hardware experiment for `ACC_OVRD_CONVERT_SIQ_TO_IRQ`, for the WDT IRQ 338, or a plan B with a minimal EL2 shell (contrary to the "no hypervisor" constraint, a user decision). **Severity: critical. Evidence available: negative/absent (V for the absence of documentation; H for the leads).**
2. **Cleanliness of the initial state and unknown sources.** No register says which FIQ is active; a single source left active by iBoot, m1n1 or macOS (pending IPI, `PMCR0.IACT`, `UPMCR0`, `UPMSR` "CTI") is enough to keep the core in a pending FIQ and prevent idle or cause storms if a higher level routes them. Mitigation: replicate `aic_init_cpu` on every core in the firmware, verify with a diagnostic FIQ handler like m1n1's `exc_fiq` on every core. **Severity: high. Evidence: V for the sequence; H for the unknown sources.**
3. **Legacy IPIs little exercised and with unconfirmed details.** The legacy branch is no longer used on t8103 either in Linux or in m1n1; AICv2 (M1 Pro/Max and later) no longer has legacy IPIs, so the "never FIQ" strategy **does not generalize** beyond t8103/AICv1 (project goal: all Apple Silicon). In addition: a single IPI per CPU without sender identity (a software multiplexer is needed), unmeasured latency, AIC index/MPIDR mapping to be confirmed, and the undocumented per-CPU views at `0x5000+`. Mitigation: test legacy IPI sending on t8103 across 8 cores with a dedicated test (cf. `L0` as reference); for AICv2 SoCs plan for software hwirqs or another solution. **Severity: medium-high. Evidence: V (code `L0`), H (current operation and performance).**

### 6.3 Suggested experiments (require the confirmed Time Machine backup)
1. With m1n1/EDK2: clear all FIQ sources like `aic_init_cpu`, mask `PSTATE.F`, execute WFI, check whether the core stays idle.
2. Legacy IPI test across all 8 cores with latency measurement.
3. WD0: RESET_EN=0, BITE at the maximum, BARK at a few ms, IRQ_EN=1, observe AIC IRQ 338.
4. Try (with caution) `ACC_OVRD.bit22` and see whether an architectural timer switches from FIQ to IRQ.
