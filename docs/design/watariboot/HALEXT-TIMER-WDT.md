# A11: HAL timer extension on the M1 watchdog (issue #19)

Date: 2026-10-13. Author: Sonnet executor (task A11), to be verified by Opus. Reads only with `gh api` and WebFetch. No clone, no Microsoft binaries or symbols, no line of code copied.

Legend: **[V: source]** = verified against the source; **[I]** = hypothesis.

Abbreviations (references read):

| Abbr. | Repo and reference |
|---|---|
| `IC` | ms-iot/imx-iotcore, branch `public_preview`, HEAD `c73879479ed0`, folder `hals/halext/` (MIT license) |
| `IE` | ms-iot/imx-edk2-platforms, branch `imx`, HEAD `4b82cdd47cde` (the BSP's ACPI tables) |
| `L` | AsahiLinux/linux, branch `asahi`, HEAD `77cb8f24c238` |
| `M` | AsahiLinux/m1n1, `main`, HEAD `fbec52a81f2b` |
| `D` | AsahiLinux/docs, `main`, HEAD `0d1f8917fa88` |
| `U` | AsahiLinux/u-boot, `drivers/watchdog/apple_wdt.c` |

Line numbers are those of `cat -n`.

---

## 1. The HAL timer interface, as the i.MX code uses it

The `nthalext.h` header is not public in the repos read. Everything that follows is **derived from usage** in the i.MX sources, so it only holds for what the i.MX code touches.

### 1.1 Entry and registration

- The extension includes `<nthalext.h>` and `"iMX7Timers.h"` **[V: IC:HalExtiMX7Timers/HalExtension.c:18-19]**.
- Entry point: `NTSTATUS AddResourceGroup(ULONG Handle, PCSRT_RESOURCE_GROUP_HEADER ResourceGroup)` **[V: IC:MX7/HalExtension.c:230-234; MX6/HalExtension.c:869-873]**. The comment says it registers the timer with the HAL.
- Enumeration: `GetNextResourceDescriptor(Handle, ResourceGroup, Previous, CSRT_RD_TYPE_TIMER, CSRT_RD_SUBTYPE_TIMER, CSRT_RD_UID_ANY)` in a loop, until NULL **[V: IC:MX7:270-282; MX6:903-913]**.
- Registration: `RegisterResourceDescriptor(Handle, ResourceGroup, &DescriptorHeader, &NewTimer)` with `NewTimer` of type `TIMER_INITIALIZATION_BLOCK` **[V: IC:MX7:352-363; MX6:299-302, 614-617]**.
- Before registering: `HalRegisterPermanentAddressUsage(PhysicalAddress, Size)` **[V: IC:MX7:294-299; MX6:261-265]**. MMIO mapping is done with `HalMapIoSpace(PhysicalAddress, Size, MmNonCached)`: in MX6 during registration **[V: MX6:271-276]**, in MX7 inside `Initialize` and only if not yet mapped **[V: MX7:416-427]**.
- Register access: `READ_REGISTER_ULONG/WRITE_REGISTER_ULONG` in MX7 **[V: MX7:211,227]**; `..._NOFENCE_...` versions in MX6 **[V: MX6:218,233]**.

### 1.2 `TIMER_INITIALIZATION_BLOCK`: observed fields

Initialization: `RtlZeroMemory(&NewTimer,...)` then the macro `INITIALIZE_TIMER_HEADER(&NewTimer)` **[V: MX7:315-316; MX6:282-283]**. Fields used:

| Field | Observed use | Source |
|---|---|---|
| `InternalData`, `InternalDataSize` | pointer and size of a private structure of the extension. The HAL **copies** it and passes it back as `TimerDataPtr` on every callback: the code passes the address of a local stack variable, so the copy is the only possible explanation [I on "copy"; V on the use] | MX7:317-318; MX6:288-289 |
| `CounterBitWidth` | counter width (32 in all cases) | MX7:319; MX6:290,592 |
| `CounterFrequency` | Hz | MX7:321; MX6:291-292,593 |
| `MaxDivisor` | 1 in all observed cases | MX7:323; MX6:294,594 |
| `KnownType` | always `TimerUnknown` | MX7:320; MX6:296,611 |
| `Identifier` | equal to the `Uid` of the CSRT descriptor (MX6 only) | MX6:297,612 |
| `Capabilities` | bit mask, see 1.3 | MX7:324; MX6:295,603-606 |
| `Interrupt.Mode`, `Interrupt.Polarity`, `Interrupt.Gsi` | set only if the capability is `TIMER_GENERATES_LINE_BASED_INTERRUPTS`. Values: `LevelSensitive`, `InterruptActiveHigh`, GSI taken from the CSRT | MX7:330-335; MX6:608-610 |
| `FunctionTable.Initialize / QueryCounter / ArmTimer / Stop / AcknowledgeInterrupt` | five callbacks, see 1.4. Not all mandatory: the MX6 timers register subsets | MX7:341-346; MX6:285-286, 585-588 |

### 1.3 Capabilities (bits)

The numeric values are defined in the CSRT of the i.MX7 BSP, with the comment "refer to nthalext.h" **[V: IE:Silicon/NXP/iMX7Pkg/AcpiTables/Csrt.aslc:40-63]**. The ones that matter to us:

| Bit | Name | Value |
|---|---|---|
| per-CPU | `TIMER_PER_PROCESSOR` | 0x1 |
| readable counter | `TIMER_COUNTER_READABLE` | 0x2 |
| writable counter | `TIMER_COUNTER_WRITABLE` | 0x4 |
| with divisor | `TIMER_HAS_DIVISOR` | 0x8 |
| pseudo-periodic | `TIMER_PSEUDO_PERIODIC_CAPABLE` | 0x10 |
| one-shot | `TIMER_ONE_SHOT_CAPABLE` | 0x20 |
| periodic | `TIMER_PERIODIC_CAPABLE` | 0x40 |
| rollover only | `TIMER_ROLLOVER_INTERRUPTS_ONLY` | 0x80 |
| line interrupt | `TIMER_GENERATES_LINE_BASED_INTERRUPTS` | 0x200 |
| MSI / internal | `TIMER_GENERATES_MSI_INTERRUPTS` / `..._INTERNAL_INTERRUPTS` | 0x400 / 0x800 |
| lossless rate change | `TIMER_LOSSLESS_RATE_CHANGE` | 0x1000 |
| always on | `TIMER_ALWAYS_ON` | 0x8000 |
| per-CPU internal data | `TIMER_PER_PROCESSOR_INTERNAL_DATA` | 0x10000 |
| generates reset | `TIMER_GENERATES_RESET` | 0x20000 |
| reset: pre-reset interrupt | `TIMER_GENERATES_PRE_RESET_LINE_BASED_INTERRUPTS` | 0x80000 |
| auxiliary | `TIMER_AUXILIARY` | 0x100000 |

Note: the values are those copied into Microsoft's `.aslc`; I did not read `nthalext.h`, so the match with the real header is [I] (the comment in the file asserts it).

Capabilities actually **used** by the code:
- Interrupt timers (i.MX6 EPIT and i.MX7 GPT): `PERIODIC_CAPABLE | ONE_SHOT_CAPABLE | GENERATES_LINE_BASED_INTERRUPTS | ALWAYS_ON` **[V: MX6:603-606; IE:iMX7 Csrt.aslc:173-176]**.
- Pure counter (i.MX6 GPT): only `TIMER_COUNTER_READABLE` **[V: MX6:295]**.
- `TIMER_PER_PROCESSOR`, `..._INTERNAL_DATA`, `GENERATES_RESET` and `PRE_RESET`: **defined but never assigned** to any i.MX timer **[V: grep of the two HalExtension.c and the two Csrt.aslc]**. So the i.MX code does not reveal how Windows treats a per-CPU timer.
- The i.MX7 GPT comment: "A 32 bit GPT up counter, which cannot generate interrupts and cannot be stopped" is in the descriptor block **[V: MX7:172-176]**, but the same GPT is registered with a line interrupt in the CSRT [V: Csrt.aslc:173-176]: the comment is old, the CSRT prevails [I].
- The EPIT timer comment: "Timer is not always on, but Windows requires an always on timer, so we must give it one" **[V: MX6:596-601]**. The code declares `TIMER_ALWAYS_ON` even for a timer that is not, and the comment says the flag is removed only if another suitable timer is provided. This means Windows demands at least one timer marked always-on among those registered [V as the intent of the comment; the internal mechanism is I].

### 1.4 Callbacks (signatures and behavior)

Types: `PTIMER_INITIALIZE`, `PTIMER_QUERY_COUNTER`, `PTIMER_ACKNOWLEDGE_INTERRUPT`, `PTIMER_ARM_TIMER`, `PTIMER_STOP` **[V: MX7:95-99]**.

| Callback | Signature | Observed contract |
|---|---|---|
| Initialize | `NTSTATUS f(PVOID TimerDataPtr)` | maps the MMIO, clears/switches off, configures clock and prescaler. The comment says a stall function cannot be used because it might depend on this same timer **[V: MX7:373-553, 481-485]** |
| QueryCounter | `ULONGLONG f(PVOID)` | reads the hardware counter **[V: MX7:556-587; MX6:503-532]** |
| ArmTimer | `NTSTATUS f(PVOID, TIMER_MODE Mode, ULONGLONG TickCount)` | "in TickCount ticks from now" generates the interrupt; `Mode` is `TimerModeOneShot` or `TimerModePeriodic` ("pseudo-periodic or one-shot" in the comment). Returns `STATUS_INVALID_PARAMETER` if TickCount does not fit in the register **[V: MX7:651-761, 699-702; MX6:696-775]** |
| AcknowledgeInterrupt | `VOID f(PVOID)` | called after the interrupt: clears the state; in one-shot it also stops the interrupt/counter **[V: MX7:589-649; MX6:777-821]** |
| Stop | `VOID f(PVOID)` | stops the timer and cleans up **[V: MX7:763-813; MX6:823-867]** |

Details useful to the project:
- **Periodic**: the i.MX6 EPIT uses hardware reload (`RLD=1`, Load register = TickCount) and remembers the mode in `PeriodInTicks` **[V: MX6:664-670, 748-764]**; the i.MX7 GPT uses "restart" mode (`FRR=0`) with compare1 = TickCount-1 **[V: MX7:728, 754-755]**. In both, periodicity is done by the hardware, not by the ISR.
- **Programming order** (MX7): switches off the timer, switches off and clears the interrupts, writes the compare, enables the interrupt, then switches the timer on with the counter cleared (`ENMOD=1`) **[V: MX7:704-758]**. So `ArmTimer` re-measures from "now" (counter reset), not relative to an absolute time.
- **Who stops the one-shot**: the extension, in `AcknowledgeInterrupt` (MX7 switches off the enables, MX6 rewrites CR without EN) **[V: MX7:617-629; MX6:809-812]**.
- The ISR does not belong to the extension: the extension does not register a handler, it only declares the GSI. No `ConnectInterrupt`-like function appears in the i.MX code **[V: grep, absent]**. The caller of `AcknowledgeInterrupt` is the HAL [I, inferred from the name and the lack of an ISR].

### 1.5 Two roles: counter and interrupt timer

In the i.MX6 CSRT the timer group has **two** descriptors with explicit names: "Counter (GPT)" and "Clock Timer (EPIT1)" **[V: IE:iMX6Pkg/AcpiTables/Csrt.aslc:64-68, 183, 199]**.
- GPT: no interrupt (`Interrupt = 0`, comment "No interrupt on this counter"), only `QueryCounter` **[V: Csrt.aslc:196; MX6:285-295]**.
- EPIT1: only interrupt and arm/stop, no `QueryCounter` **[V: MX6:585-588, 603-606]**.
- So for the i.MX6 case the HAL has a (readable) counter and a countdown (programmable) timer, as distinct entities. The i.MX7 case instead has a single registered GPT (without `TIMER_COUNTER_READABLE`), plus a GTDT for the ARM generic timer **[V: IE:iMX7Pkg/AcpiTables/Gtdt.aslc:17-39 (PL1 GSIV 29/30, virtual 27, PL2 26)]**. The provider of the counter in the i.MX7 case is most likely the generic timer via the GTDT [I].

### 1.6 How the extension is hooked (CSRT, INF)

- The resource group has `VendorId`, `DeviceId` (i.MX6 0x0101, i.MX7 0x0112) **[V: IE:iMX6 Csrt.aslc:170-181; iMX7 Csrt.aslc:139-150]**; the INF matches `ACPI\NXP0112` and writes `OSExtensionDatabase\VEN_NXPI&DEV_0112\HALExtension` = `%13%\HalExtiMX7Timers.dll` **[V: IC:MX7/HalExtiMX7Timers.inf:28-36]**; Computer class, `PnPLockdown=1`, `ExcludeFromSelect=*` **[V: .inf:6-14]**.
- The format of the descriptor after the header is **free** (decided by the extension author): i.MX6 and i.MX7 have different layouts, even for the timer **[V: MX6:51-62 vs MX7:53-74]**; the i.MX7 CSRT says "to maintain backward compatibility only new member here" **[V: IE:iMX7 Csrt.aslc:96]**. Consistent with the Microsoft page: "format and use ... defined by the HAL extension writer" **[V: learn.microsoft.com ... acpi-system-description-tables, CSRT section]**.
- The GSI in the i.MX7 CSRT is `DEFINE_IMX7_INTERRUPT(55)` = 55 + `TOTAL_PRIVATE_INTERRUPT` (32) **[V: IE:iMX7Pkg/Include/iMX7.h:48-49,84]**, i.e. **GIC SPI number + 32 = GIC ID**. So the GSI is a GIC ID, and it is delivered by the GIC described in the MADT (the i.MX7 MADT has GICC and GICD **[V: IE:iMX7 Madt.aslc:20-34]**). This is the point of §3.2.

### 1.7 What the i.MX code does NOT say (interface gaps)

- No use of `TIMER_PER_PROCESSOR`: the per-CPU semantics are unknown from the code.
- No example of a **64-bit** timer or with `TickCount` beyond 32 bits (both reject/assume 32 bits **[V: MX7:699-702; MX6:739-742]**). The maximum value accepted by `ArmTimer` depends on `CounterBitWidth` and `MaxDivisor` [I].
- No example of how the HAL chooses between a GTDT timer and an extension timer when both are present (i.MX7). [I: the HAL prefers the one marked ALWAYS_ON/of suitable capability.]
- No indication of behavior on an already-running system (frequency change, `TIMER_LOSSLESS_RATE_CHANGE`).
- No example using `Interrupt.Mode = EdgeTriggered` or `ActiveLow`.

---

## 2. The M1 watchdog from the Asahi sources

### 2.1 Register map (base `0x23d2b0000`, size `0x4000`)

DT: `watchdog@23d2b0000`, `compatible = "apple,t8103-wdt", "apple,wdt"`, `reg = <0x2 0x3d2b0000 0x0 0x4000>`, `clocks = <&clkref>`, `interrupts = <AIC_IRQ 338 IRQ_TYPE_LEVEL_HIGH>` **[V: L:arch/arm64/boot/dts/apple/t8103.dtsi:1223-1229]**. `clkref` is a 24 MHz `fixed-clock` **[V: t8103.dtsi:390-395]**.

Registers according to the Linux driver **[V: L:drivers/watchdog/apple_wdt.c:37-52]**:

| Offset | Name | Notes |
|---|---|---|
| 0x00 | WD0_CUR_TIME | counter, +1 per clkref tick, writable **[V: :27-28]** |
| 0x04 | WD0_BITE_TIME | reset compare |
| 0x08 | WD0_BARK_TIME | interrupt compare (**exists only for WD0**) |
| 0x0c | WD0_CTRL | |
| 0x10 / 0x14 / 0x1c | WD1_CUR / BITE / CTRL | the one Linux actually uses |
| 0x20 / 0x24 / 0x2c | WD2_CUR / BITE / CTRL | |

CTRL: bit 0 `IRQ_EN`, bit 1 `IRQ_STATUS` (write 1 to clear), bit 2 `RESET_EN` **[V: :50-52, 32-35]**.

Width: 32 bit. The driver computes `max_hw_heartbeat_ms = U32_MAX / clk_rate * 1000` **[V: :183]** (at 24 MHz that is 178 s, i.e. 178 whole seconds). `D` confirms "24MHz 32-bit timer ... maximum timeout is just short of three minutes" **[V: D:docs/hw/soc/wdt.md:9,13]**. Full counter period: 2^32 / 24e6 = 178.96 s [calculation].

### 2.2 Interrupt: what is documented

- Driver comment: "WD0 additionally supports a configurable interrupt. This information can be used to implement pretimeout support at a later time." **[V: apple_wdt.c:24-25]**. In the next paragraph: with `IRQ_EN` and `CUR_TIME >= BARK_TIME` the interrupt fires and `IRQ_STATUS` is set; it is cleared by writing 1 to `IRQ_STATUS` **[V: :32-35]**. The comment mentions `WD1` for CUR/BARK by a typo: the BARK register exists only at 0x08 [V as layout; the typo is I].
- **The driver never uses the interrupt**: the probe requests no IRQ and never writes `IRQ_EN` or BARK **[V: :157-196, grep `irq`]**. The DT declares it anyway (and the binding requires it: `interrupts: maxItems: 1`, `required`) **[V: L:Documentation/devicetree/bindings/watchdog/apple,wdt.yaml:45-52]**.
- No other source read uses it: the search `wdt bark` in the AsahiLinux organization yields only `hv_wdt.c`, a software watchdog of the hypervisor (breadcrumbs), and the U-Boot driver **[V: gh search; M:src/hv/hv_wdt.c:51-60]**. In the U-Boot driver the offset 0x14 is called `BARK_TIME` but it is the **reset** compare of WD1 (`start` writes the timeout there and `RESET_EN` at 0x1c) **[V: U:12-14,31-45]**: the bark/bite names are used inconsistently across sources, so **names alone cannot be trusted**.
- Therefore: the "WD0 + BARK + IRQ 338" mode is **documented only by a comment, never exercised** by any public code I found. It remains **[I]** until there is proof on hardware.

### 2.3 Reset: what must NOT happen

- `RESET_EN` + `CUR >= BITE` => reset of the entire machine **[V: apple_wdt.c:29-31]**. Linux, with `RESET_EN` written to WD1_CTRL, sees the reset "after up to ~120-125 ms" **[V: :130-135]**.
- Discrepancy on the reset type: Linux says "WD0 resets the machine to recovery mode... WD1 and WD2 trigger a normal machine reset" **[V: :22-24]**; `D` says the registers 0x0/0x4/0xc **and 0x20/0x24/0x2c** lead to recovery **[V: D:wdt.md:15]**. On WD2 the two sources **contradict each other**. For us: any `RESET_EN` on any WDx is a risk.
- Initial state: the initial macho (m1n1) starts with the watchdog active, compare 120 s **[V: D:wdt.md:7,9]**. m1n1 disables it by writing 0 to `base+0x1c` (WD1) and, for `wdt-version` 2 or 3, also to a second register `reg[2]` **[V: M:src/wdt.c:10,24-31,33-48]**. **On WD0 and WD2 m1n1 writes nothing**: their initial state is **unknown** [I]. On t8103 `wdt-version` is not read by us [I].
- m1n1's `wdt_reboot` uses WD1: alarm 0x100000, count 0, ctl 4 **[V: M:src/wdt.c:51-59]**: confirms WD1 = immediate reset.

### 2.4 How to get a periodic interrupt without reset (theoretical recipe)

All **[I]**, derived from the semantics of the comment:
1. `WD0_CTRL = 0` (RESET_EN=0, IRQ_EN=0), `IRQ_STATUS` cleared by writing 2.
2. `WD0_BITE_TIME = 0xFFFFFFFF` (the maximum) to stay far from reset even if `RESET_EN` were enabled by mistake.
3. `WD0_CUR_TIME = 0`, `WD0_BARK_TIME = N`, `WD0_CTRL = IRQ_EN`.
4. On interrupt: write 2 (IRQ_STATUS) to CTRL; re-arm with `CUR_TIME = 0` (or `CUR = CUR - N`) and BARK = N.
Open questions (no source): whether the `IRQ_STATUS` bit is a pending latch or follows the comparison (if `CUR >= BARK` remains true, the status may rise again right after clearing); the state of WD0_CTRL at boot; whether the comparison is `>=` with wrap or the counter continues past BARK; whether writing CTRL with read-only bits has side effects; whether the counter stops in sleep states. None of these is settled by the sources.

---

## 3. Design: the watchdog as the timer of an HAL extension

### 3.1 Mapping onto the interface (proposal)

Following the i.MX6 precedent (a readable counter + an interrupt timer) **[V: §1.5]**, proposal of **two** registrations in the same resource group:

| Role | Hardware source | Capabilities | Notes |
|---|---|---|---|
| Readable counter | `CNTPCT_EL0` (physical counter, 24 MHz) **or** `WD0_CUR_TIME` | `TIMER_COUNTER_READABLE` | `CNTPCT` is 64-bit and does not generate interrupts; WD0_CUR is 32-bit, writable by anyone (even macOS/m1n1) and wraps after 179 s: **not recommended as a counter** [I]. The ARM counter should be exposed through the GTDT (standard, section 3.5) or with an extension registration that reads `CNTPCT_EL0` [I; I have no public example of an extension that reads a system register] |
| Interrupt timer | WD0 (CUR/BARK/CTRL) | `ONE_SHOT_CAPABLE | PERIODIC_CAPABLE | GENERATES_LINE_BASED_INTERRUPTS | ALWAYS_ON` | like the i.MX6 EPIT **[V: MX6:603-606]** |

Registration parameters of the interrupt timer: `CounterBitWidth = 32`, `CounterFrequency = 24000000`, `MaxDivisor = 1`, `KnownType = TimerUnknown`, `Interrupt.Mode = LevelSensitive`, `Polarity = InterruptActiveHigh` (consistent with the DT's `IRQ_TYPE_LEVEL_HIGH` **[V: t8103.dtsi:1228]**), `Gsi = <to be decided, §3.2>`.

This proposal parallels what the i.MX BSP does; whether it works on the M1 is [I].

### 3.2 The GSI: who delivers it? (link with A1)

Facts:
- The i.MX code declares a GSI that is a **GIC ID** and leaves delivery to the HAL, which already knows the GIC from the MADT **[V: §1.6]**. The extension has neither an ISR nor a ConnectInterrupt **[V: §1.4]**.
- IRQ 338 is an **AIC** number (`AIC_IRQ 338`), not a GIC one **[V: t8103.dtsi:1228]**. On the M1 there is no GIC: a layer is needed that delivers an AIC interrupt to Windows.
- The question of whether an HAL extension can provide a non-GIC interrupt controller is **A1** and is **undeterminable** from public sources (low confidence) **[V: re/f0-halext-public-docs.md §5]**. No controller registration API appears in the i.MX code **[V: grep]**.

So three scenarios, with the same ingredients but a different outcome:

| Scenario | How the GSI arrives | Status |
|---|---|---|
| S1: an extension/driver provides an AIC controller to Windows | the HAL delivers GSI 338 (mapped into its GSI space) to the timer extension as for the i.MX | depends on A1: **undeterminable** |
| S2: the firmware/EDK2 (or a minimal shell) emulates a GICv2/v3 on top of the AIC | the GSI is a virtual GIC ID; the HAL timer receives it as for the i.MX. But an emulated GIC requires intercepting MMIO accesses: EL2 is needed (contrary to the "no hypervisor" constraint) [I] | plan B, the user decides |
| S3: no controller recognized | the timer extension declares the interrupt but nobody delivers it: the timer never fires, Windows hangs at the first `ArmTimer` [I] | case to exclude with A1/V4 |

**Critical dependency: without a positive answer to A1 (or an S2 shell), the watchdog has no way to reach Windows as an interrupt, regardless of the correctness of the timer design.** In addition: the `Interrupt.Gsi` field is a `ULONG` that the HAL interprets in the GSI space of the registered controller [I].

### 3.3 A single timer for the whole SoC

What is known:
- The WDT is **a single MMIO block** at SoC level with a single AIC IRQ (338) **[V: DT]**. The AIC can pin the destination core of an hwirq with `TARGET_CPU` (affinity), already verified in `re/f0-m1-fiq-irq.md` §3.3 (`aic_irq_set_affinity`, referenced there).
- In the 4-core i.MX6 BSP (HummingBoard i.MX6Quad, SABRE i.MX6Q listed in the README **[V: IC:README, platform table]**) the CSRT registers **only one** interrupt timer (EPIT1) **without** `TIMER_PER_PROCESSOR` **[V: IE:iMX6 Csrt.aslc:199-213; MX6:603-606]**, and the `IE` tree has no `Gtdt.aslc` for iMX6 (only for iMX7) **[V: listing of the imx-edk2-platforms tree]**: the i.MX6 is a Cortex-A9, without the ARM generic timer [I]. So, for the SMP i.MX6, the system timer was **single and global**. That Windows IoT actually ran on more than one core with this configuration is [I]: the README lists i.MX6 Quad as supported but does not say on how many cores.
- How Windows distributes the tick among cores with a single global timer: **not documented in the sources read**. [I: the HAL uses the timer registered on the BSP and sends IPIs to the other cores when needed; this requires working IPIs, see A4 and `re/f0-m1-fiq-irq.md` §4.]

Consequences for the M1 [I]:
1. Distribution to the cores happens with AIC IPIs (legacy IPI on t8103, see `re/f0-m1-fiq-irq.md` §4) as far as the HAL is concerned, not the extension.
2. If Windows demands a **per-processor** timer (`TIMER_PER_PROCESSOR`) for local ticks or for the timer-wheel of precise deadlines, a single WD0 is not enough. There is no source that says so or rules it out.
3. Affinity of IRQ 338: assign it to a single fixed core (the boot CPU) and leave the rest to IPIs.

### 3.4 One-shot, periodic and the 32-bit wrap

**ArmTimer(Mode, TickCount)** to be implemented on WD0 [I]:
- `TickCount` <= 0xFFFFFFFF, otherwise `STATUS_INVALID_PARAMETER` as in i.MX **[V: MX7:699-702]**. At 24 MHz the maximum is 178 s: the register suffices for every Windows tick wait; for longer waits the HAL can re-arm [I].
- **One-shot**: `RESET_EN=0`, write `CUR_TIME = 0`, `BARK_TIME = TickCount`, `CTRL = IRQ_EN`. The comparison is `CUR >= BARK`, so the minimum acceptable value is > 0 (with BARK=0 it fires immediately).
- **Periodic**: there is no automatic reload. There are two ways:
  - (a) "pseudo-periodic" in `AcknowledgeInterrupt`: clear `IRQ_STATUS`, then `CUR_TIME = 0` (or subtract the period so as not to lose ticks) and leave BARK unchanged. The i.MX6 EPIT precedent reloads in hardware **[V: MX6:664-670]**; here the reload is manual, and the pseudo-periodicity comes from the ISR latency (accumulated drift);
  - (b) declare **only** `ONE_SHOT_CAPABLE` and (as the `TIMER_PSEUDO_PERIODIC_CAPABLE` bit suggests **[V: IE:iMX7 Csrt.aslc:47]**) let the HAL re-arm: the presence of that bit suggests the HAL knows how to emulate a periodic with re-arms [I].
  - Recommendation: declare `PSEUDO_PERIODIC` or one-shot, **not** `PERIODIC_CAPABLE`, so as not to promise its precision. [I]
- **32-bit wrap**:
  - by clearing `CUR_TIME` at each `ArmTimer` the counter never wraps (max 178 s);
  - the write of `CUR_TIME` and real time are not atomic: between the read/clear and the arming, the ticks of a couple of MMIO accesses elapse (not quantified) **[I]**; system time drift derived from this timer must be avoided: the time base (QPC / interrupt time) should come from a 64-bit counter that is never cleared, i.e. `CNTPCT_EL0` or its copy in the AIC (to be verified, note in `re/f0-m1-fiq-irq.md` §2) **[I]**;
  - alternative without clearing: BARK = current CUR + N (sum modulo 2^32). But the **non-modular** `>=` comparison fires immediately if the sum exceeds 2^32 **[V: `>=` comparison in the comment; the behavior at wrap is I]**; so: if CUR + N does not fit in 32 bits, CUR must be cleared or the timer armed in two steps.
- **AcknowledgeInterrupt**: write `IRQ_STATUS` (bit 1) to clear; in one-shot also `IRQ_EN=0`, as the i.MX7 extension does **[V: MX7:617-629]**.
- **Stop**: `CTRL = 0` and clear `IRQ_STATUS`.
- **Initialize**: map the MMIO (`HalMapIoSpace`, `MmNonCached`), switch off WD0 as above, and **do not touch WD1/WD2** (reset). Force `RESET_EN=0` and BITE to the maximum. Do not use stall functions (same warning as the i.MX code **[V: MX7:481-485]**).

### 3.5 Resolution and accuracy

- Resolution: 1 tick = 41.67 ns (24 MHz) **[V: DT + calculation]**. The value declared in the `CounterFrequency` field is 24,000,000. Windows declares system times at 100 ns: the hardware resolution is much finer than the system tick (15.6 ms or 1 ms typical, [I]).
- Absolute accuracy: the reference clock is the same 24 MHz oscillator as the ARM counter (in the DT is it the same `clkref` that also feeds the generic timer nodes? **not verified**: in the t8103 DT the `timer` node has no `clock-frequency`; `CNTFRQ` = 24 MHz is reported in `D`/m1n1 [V: re/f0-timer-irq-hw.md §5]). That WD0 and `CNTPCT` share the same clock origin is [I].
- Drift and latency: the MMIO access latency to the WDT is not quantified in any source [I]; the error for each arming is at least a few MMIO accesses + AIC interrupt latency + the IPI round trip (if the tick must reach another core).
- Jitter due to ISR latency: if the reload is manual (periodic), the error accumulates; for this reason the time base must be the 64-bit counter, not the WD.
- Is the WD block in a domain that survives sleep states? Unknown [I]. If the counter stops in idle, `ALWAYS_ON` would be a false promise.

---

## 4. Risks, prerequisites, checks

### 4.1 Risks

| # | Risk | Severity | Status |
|---|---|---|---|
| R1 | No interrupt controller recognized by Windows that delivers AIC IRQ 338 (A1) | **critical** | undeterminable |
| R2 | The WD0 BARK interrupt does not work as described: never tested in any public code | **critical** | only a comment **[V]** |
| R3 | `RESET_EN`/BITE on WD0 (recovery) or WD1/WD2 (reset): a bug in the extension reboots the Mac, WD0 and sometimes WD2 into recovery | high | **[V]** documented; initial state of WD0/WD2 unknown |
| R4 | The watchdog is not ours: macOS/m1n1/iBoot use it; if it stays active and unserviced the Mac reboots after 120 s (WD1). The extension should first make sure WD1 is off (m1n1 does this) | high | **[V]** |
| R5 | A single timer for the whole SoC (no evidence on `TIMER_PER_PROCESSOR`) | medium-high | **[I]** |
| R6 | The HAL demands an `ALWAYS_ON` timer and a counter: if WD0 is not truly always-on (sleep, idle), the declaration is false | medium | **[V]** (requirement); hardware state **[I]** |
| R7 | Wrap and the non-modular `>=` comparison complicate the periodic | medium | design |
| R8 | Signing of the HAL extension (special EKU) and loading with test-signing: undeterminable **[V: re/f0-halext-public-docs.md §1]** | high | V4 |
| R9 | AICv2 (M1 Pro/Max and later): the WDT hwirq and the block structure change; "all Apple Silicon" is not guaranteed. The compatibles for newer SoCs all point to `apple,t8103-wdt` as a fallback **[V: apple,wdt.yaml:16-25]**, but the IRQ and AIC map must be re-read for each SoC | medium | **[I]** |
| R10 | Lack of evidence that Windows accepts a system timer that arrives via IPI/AIC on a single core | medium | **[I]** |

### 4.2 Prerequisites (in order)

1. **A1/V4**: Windows loads an extension hooked to a CSRT group of ours and recognizes a non-GIC interrupt controller (or an S2 shell decided by the user).
2. **HW**: proof on M1 of the WD0 interrupt (§4.3, step H1), with backup confirmed.
3. A working AIC controller for Windows (A4), even for a single IRQ.
4. A GTDT or an extension that provides the 64-bit counter and system time, without generic timer interrupts (FIQ).
5. Design decision on how to prevent Windows from using the ARM generic timer (which arrives as FIQ): do not publish the GTDT, or publish it with unusable timers. If the GTDT is absent, Windows ARM64 might refuse to boot, or use the generic one anyway [I: no source read]. For the i.MX7 the GTDT and the extension GPT coexist **[V: §1.5]** and the HAL chose one of the two: which one and by which rule is unknown.

### 4.3 What can be verified in QEMU (A8 AIC model) and what cannot

In QEMU (with an AIC model + a WDT block modeled by us), **after A8** [I]:
- Loading the extension (INF, `OSExtensionDatabase`, CSRT with our ID): yes, it is a Windows matter, not a hardware one.
- The **structure of the interface**: whether `RegisterResourceDescriptor` accepts our block, which fields are read, whether `ArmTimer`/`AcknowledgeInterrupt` are called with the expected parameters, the requested one-shot/periodic mode, the effective sizes of `TickCount`. With a WDT modeled by us one can count how many times and with what values Windows calls `ArmTimer` in a typical boot.
- Whether Windows goes ahead without a GIC with only the modeled AIC (part of V2/V4), and whether an extension timer replaces the GTDT one.
- The GSI -> controller -> `AcknowledgeInterrupt` flow.
- The behavior of system time when there is a single global timer with emulated IPIs.

Only on hardware:
- Whether WD0 really generates IRQ 338 with `IRQ_EN`/BARK and how `IRQ_STATUS` behaves (latch vs level), the wrap, the initial state of WD0/WD2, the effect of `RESET_EN` on WD0.
- The WDT MMIO latency, the accuracy, the tick jitter.
- The `WD0` block in idle (does it stop or not).
- Whether the WDT clock and `CNTPCT` have the same origin.
- The AIC affinity of IRQ 338 and delivery to a chosen core.
- The state left by iBoot/m1n1/macOS.
A QEMU model of the WDT written by us reflects our **hypotheses**, so QEMU cannot validate the hypothetical points of §2.4: H1 is needed.

**H1 (proposed hardware test, register reads/writes only, no writes to disk)**, to be run with m1n1 in proxy mode and **only after the Time Machine backup confirmed by the user** and explicit confirmation before touching the boot (see protocol in `re/f0-timer-irq-hw.md` §5): (1) read `WD0_CTRL` and `CUR` to understand whether WD0 is already counting; (2) set `RESET_EN=0`, BITE=0xFFFFFFFF; (3) BARK to a few ms, `IRQ_EN=1`; (4) observe AIC hwirq 338 (HW_STATE) and `IRQ_STATUS`; (5) restore the values read at step 1. The risk is that of R3.

---

## 5. Conclusion

**Feasibility: possible on paper, not demonstrable today.** The HAL timer interface is fairly simple (five callbacks, one registration structure with frequency, width, capabilities and GSI) and the watchdog has a 24 MHz counter, a compare register and a level IRQ: the mapping is natural. But the project rests on two unverified hypotheses.

**Critical dependencies:**
1. **A1: who delivers the GSI.** The timer extension declares a GSI and leaves delivery to the controller already known to the HAL (the GIC in the i.MX case). On the M1 IRQ 338 belongs to the AIC and Windows does not know the AIC: without a solution for the controller (extension, firmware or S2 shell), the interrupt does not arrive.
2. **The WD0 bark is only a comment.** No public code uses it. It must be proven on hardware (H1), with the risk of recovery mode.
3. **A single global timer:** the i.MX6 precedent (4 cores, a single timer without `TIMER_PER_PROCESSOR`) indicates it can be done, but a working IPI (A4) is needed, and whether Windows accepts it on the M1 is a hypothesis.
4. The counter and system time must not depend on WD0 (32 bit, 179 s): `CNTPCT` or equivalent is needed, with the GTDT/FIQ question to be resolved.

### Out of scope but useful (to be evaluated)

`Wack0/NtHalExts` (GitHub) contains HAL extensions for Windows 8.x ARMv7: `halextgit` (ARM generic timer, reads GTDT) and `halextbcm2709` (BCM2709 **interrupt controller**, "backported from rs1-era HAL"). The README says the interrupt controller extensions "were added in 789x but removed again in 799x" and require pattern matching on unexported functions **[V: README of Wack0/NtHalExts]**. It is therefore relevant to A1/A4 (it indicates that the interrupt controller API existed, but is not stable in recent builds), but the repo includes headers and libraries from Microsoft's Windows 8 DDK ("reproduced here"): **I have not used or copied anything** and it must not be included in our repository. It was not read beyond the README.

## Sources

1. `ms-iot/imx-iotcore` branch `public_preview`: `hals/halext/HalExtiMX6Timers/{HalExtension.c,iMX6Timers.h}`, `hals/halext/HalExtiMX7Timers/{HalExtension.c,iMX7Timers.h,HalExtiMX7Timers.inf}`.
2. `ms-iot/imx-edk2-platforms` branch `imx`: `Silicon/NXP/iMX6Pkg/AcpiTables/Csrt.aslc`, `Silicon/NXP/iMX7Pkg/AcpiTables/{Csrt.aslc,Gtdt.aslc,Madt.aslc}`, `Silicon/NXP/iMX7Pkg/Include/iMX7.h`.
3. `AsahiLinux/linux` branch `asahi`: `drivers/watchdog/apple_wdt.c` (initial commit `4ed224aeaf`, 5 January 2022, Sven Peter), `arch/arm64/boot/dts/apple/t8103.dtsi`, `Documentation/devicetree/bindings/watchdog/apple,wdt.yaml`.
4. `AsahiLinux/m1n1`: `src/wdt.c`, `src/hv/hv_wdt.c`. `AsahiLinux/docs`: `docs/hw/soc/wdt.md`. `AsahiLinux/u-boot`: `drivers/watchdog/apple_wdt.c`.
5. https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables (GTDT, CSRT, HAL extension).
6. `Wack0/NtHalExts` (README only).
