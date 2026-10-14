# F0 / A9: the `CONVERT_SIQ_TO_IRQ` bit and other ways to get the tick as an IRQ (with protocol HW-1)

Task A9 (issue #17), with the hardware test protocol HW-1 (issue #20). Read-only research with `gh api` and WebSearch. No downloads, no clones, no Microsoft binaries.

Conventions: **[V: repo/file:lines@ref]** = verified by reading that file; **[H]** = hypothesis or deduction. Lines are those of `cat -n`.

| Abbreviation | Repo and reference |
|---|---|
| `M` | AsahiLinux/m1n1, `main`, HEAD `fbec52a81f2b12a5cf75f83a653d46df0bbc36ce` |
| `D` | AsahiLinux/docs, `main`, HEAD `0d1f8917fa88745d62a4c05d802c4a7298c8616b` |
| `L` | AsahiLinux/linux, branch `asahi` |
| `X` | apple-oss-distributions/xnu, `main`, HEAD `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea` |

---

## 1. The register and the bit in m1n1

### 1.1 Definition
- Register: `SYS_IMP_APL_ACC_OVRD = sys_reg(3, 5, 15, 6, 0)`, i.e. `s3_5_c15_c6_0` **[V: M:src/cpu_regs.h:541]**.
- Long name: in the proxyclient it is `ACC_OVRD_EL1`, "Apple Core Cluster Override" **[V: M:proxyclient/m1n1/apple_regs.json:253]**. Next to it is `ACC_EBLK_OVRD_EL1` (s3_5_c15_c6_1), "E-Block Override" **[V: apple_regs.json, next line]**.
- The bit: `#define ACC_OVRD_CONVERT_SIQ_TO_IRQ BIT(22)` **[V: M:cpu_regs.h:563]**. It has no description: in the file the bits of this register have no comments.
- Neighbouring bits, all from `cpu_regs.h`:

| Bit | Name in m1n1 | Notes |
|---|---|---|
| 19 | `DVFM_SAFE_VOL` | **[V: cpu_regs.h:560]** |
| 20 | `FRC_ZOUT_AMX_PWR_DN_TMR` | **[V: :561]** |
| 21 | `RC_ACK_INTR_LOCK` | **[V: :562]** |
| **22** | **`CONVERT_SIQ_TO_IRQ`** | **[V: :563]** |
| [24:23] | `VOL_UPD_ACK` | **[V: :564-565]** |
| [26:25] | `POWER_DOWN_CPM` | used in `cpu_sleep` **[V: M:src/utils.c:256]** |
| [28:27] | `CPM_WAKE_UP` | **[V: :568-569]** |
| 29, 30, 31 | `DISABLE_CLK_DTR`, `DISABLE_PSW_OFF_ABORT`, `RESET_CPM` | **[V: :570-572]** |

  The context is a **cluster power-management** register (CPM, SRM, retention, deep sleep, DVFM, PMGR). The closest interrupt-related bit is `RC_ACK_INTR_LOCK` (bit 21). So `CONVERT_SIQ_TO_IRQ` probably lives in a part of the cluster that talks to the power manager, not in the core's timer path [H].
- Where the name comes from: commit `b067f2697` (Hector Martin, 18 April 2023) says only "Add ACC_OVRD register. Not used yet, but might be in the future" **[V: gh api commits/b067f2697]**. The names (also `PRE_RESET_CLK_CNTS`, `CNVT_INC_TO_FULL_UPD`, ...) are not in the public XNU sources: the XNU header knows only bits 13-18, 25-29, 32 and 34 **[V: X:pexpert/pexpert/arm64/apple_arm64_regs.h:29-42]**. The origin of the names is not documented [H: probably an internal source or a string dump, not verifiable].
- The Asahi documentation of `ACC_OVRD` lists only the deep-sleep bits (13-18, 25-29, 32, 34) and **does not mention bit 22** **[V: D:docs/hw/cpu/system-registers.md:361-376]**.

### 1.2 All uses in m1n1 (src/ and proxyclient/)
Search for `CONVERT_SIQ` in the organization: **a single occurrence, the definition** **[V: gh search `CONVERT_SIQ_TO_IRQ+org:AsahiLinux`]**. Uses of `ACC_OVRD` in general:
- `M:src/utils.c:254-257`: in `cpu_sleep(deep)` with `SLEEP_GLOBAL`, `reg_mask(ACC_OVRD, DIS_L2_FLUSH_ACC_SLEEP_MASK, PWR_DN_SRM(3) | DIS_L2_FLUSH_ACC_SLEEP(2) | TRAIN_DOWN_LINK(3) | POWER_DOWN_CPM(3) | DISABLE_PIO_ON_WFI_CPU | DEEP_SLEEP)` **[V]**. Bit 22 is not touched.
- `M:proxyclient/m1n1/hv/__init__.py:663`: `ACC_OVRD_EL1` (and `ACC_CFG_EL1`) are in the hypervisor's `ro` set, i.e. guest writes do not go through **[V: __init__.py:655-664, interpretation of the `ro` set as "writes ignored" is [H]]**.
- `M:proxyclient/m1n1/apple_regs.json:253`: name only, **no fieldset** for bit 22 **[V]**.
- No use in `src/chickens*.c` **[V: grep]**.

### 1.3 Value observed on M1
- Asahi's dumps report `SYS_APL_ACC_OVRD s3_5_c15_c6_0 = 0x180010102001c207`, identical on Icestorm and Firestorm, both in m1n1 (after the chicken bits) and **under Darwin 20.3.0 (XNU 7195.81.3)** **[V: D:docs/hw/cpu/system-register-dumps.md:239 and :610]**.
- The low bytes are `0x2001c207`: the byte for bits [23:16] is `0x01`, so **bit 22 is 0** in both dumps **[V: calculation on the value above]**.
- Consequence: on M1, macOS receives the timer as FIQ with bit 22 = 0 (factory value or set by the firmware) **[V as data]**. What happens with bit 22 = 1 is unknown [H].
- It is not known whether the register is per core or per cluster. The name ("Core Cluster") and the existence of `ACC_EBLK_OVRD` suggest cluster level, with one copy per E/P block, but identical content between Icestorm and Firestorm does not prove it [H].

---

## 2. Where else to look: what turns up

### 2.1 XNU (Apple open source code)
- **No occurrence** of `CONVERT_SIQ`, `SIQ_CFG` or `s3_4_c15_c10_4` in XNU **[V: gh search on `X`, three empty searches]**. XNU never touches bit 22 of `ACC_OVRD`: all uses are in the deep sleep path **[V: X:osfmk/arm64/machine_routines_asm.s:1055-1081]**.
- `ARM64_REG_CYC_OVRD_irq_*/fiq_*` ("disable" mode 2 on bits [23:22] and [21:20]) are in XNU, which sets them to zero at startup (`and ... ~(irq_mask|fiq_mask)`) **[V: X:osfmk/arm64/start.s:789; apple_arm64_regs.h:45-48]** and sets them to "disable" before deep WFI **[V: machine_routines_asm.s:1036-1037]**. It is the only IRQ/FIQ control that XNU uses and it concerns wake-up, not the signal type [H].

**How XNU handles the kernel timer on Apple Silicon [V]:**
1. The kernel's "decrementer" is the **EL0 virtual timer**: `ml_set_decrementer` writes `CNTV_TVAL_EL0` **[V: X:osfmk/arm64/machine_routines.c:2393-2405]**. For SoCs with `HAS_APPLE_GENERIC_TIMER` (headers `H15.h` and `H16.h`) it uses Apple S3_x registers instead (`S3_1_C15_C15_4` = TVAL, `S3_1_C15_C0_5` = CTL, etc.) **[V: machine_routines.c:2375-2388; H15.h:49 "Supports 24 MHz Apple timer"]**. An XNU test says that with `AIDR_EL1` bit 32 (AGT) the counter runs at 1 GHz, otherwise at 24 MHz **[V: X:tests/apple_generic_timer.c:19-25]**.
2. `fiq_context_init` enables the virtual timer (`CNTV_CTL_EL0 = ENABLE`) and **masks the physical timer** (`CNTP_CTL_EL0 = IMASKED`) as a precaution **[V: machine_routines.c:2627-2646]**.
3. The timer interrupt arrives as a **FIQ**: the vectors point to `fleh_fiq` **[V: X:osfmk/arm64/locore.s:542-547, 805-809]**, which calls `sleh_fiq`. This classifies: fast IPI (`S3_5_C15_C1_1 & PENDING`), then timer (`ml_get_timer_pending`: `CNTV_CTL_EL0.ISTATUS`), then PMI; everything else falls into the "we don't know if it's a timer" branch and is treated as `rtclock_intr` **[V: X:osfmk/arm64/sleh.c:2583-2672; machine_routines.c:2548-2551]**.
4. True IRQs (`sleh_irq`) go to the platform's interrupt controller driver, which is not in the public source (the AIC part is in a closed kext) [H].
5. IPIs are Fast IPIs via sysreg (`HAS_IPI`, `gFastIPI`) and they also arrive as FIQs **[V: sleh.c:2594-2642]**.
6. WFE uses the arch timer's "event stream" (`CNTKCTL_EL1.EVNTEN`), which is an event for WFE and not an interrupt **[V: machine_routines.c:2600-2614]**.

**So: XNU never uses a tick as IRQ on Apple Silicon.** It uses the ARM virtual timer as a FIQ. There is no MMIO timer in use in the kernel path, and `pe_arm_init_timer` registers only an empty `tbd_ops` under arm64 **[V: X:pexpert/arm/pe_identify_machine.c:247-275]**.

### 2.2 Linux Asahi
- `irq-apple-aic.c`: no references to `SIQ`, `ACC_OVRD` or `CYC_OVRD`; the only IMP-DEF registers between timer and FIQ are `VM_TMR_FIQ_ENA_EL2` and Fast IPI **[V: L:drivers/irqchip/irq-apple-aic.c, grep]**.
- The only comment on why it cannot be masked: "Only the guest timers have real mask bits, unfortunately." **[V: L:irq-apple-aic.c:502]**. There is no comment explaining why the timer cannot go to IRQ; it is simply documented as wired to FIQ **[V: D:docs/hw/soc/aic.md:71-73, "bypasses AIC. It is wired straight to FIQ"]**.
- A boot message in the documentation shows "CPU features: detected: FIQs" and "AIC: initialized with 896 IRQs, 2 FIQs, 2 IPIs, 32 vIPIs" **[V: D:docs/platform/dev-quickstart.md:653,669]**.

### 2.3 Asahi documentation and public projects
- `D:docs/hw/cpu/system-registers.md`: **only one register talks about core IRQ/FIQ**, and it is `s3_4_c15_c10_4`: "labeled SIQ_CFG_EL1 in m1n1. If a core writes 0x3 to its copy of the register, AICv2 will not send IRQs to that core. FIQs unaffected, as those are part of the core complexes themselves. (0x0 and 0x2 are known values that will enable IRQs on a core, but 0x0 seems to cause weird EL1 problems)" **[V: system-registers.md:735-737]**. That is, **FIQs are internal to the core complex, IRQs go through an external block** [V as text, the interpretation is the author's].
- Full name in m1n1: `SIQ_CFG_EL1`, "System Interrupt Configuration (EL1)", encoding (3,4,15,10,4) **[V: apple_regs.json:227]**. m1n1 writes `2` to it on the cores with `siq_cfg` ("Enable IRQs (at least necessary on t600x)", "XXX 0 causes pathological behavior in EL1, 2 works") **[V: M:src/chickens.c:228-232]**. The `siq_cfg` flag is set in the structure with `.siq_cfg = true` at line 78, in the `features_m1` block (starting at line 69) **[V: chickens.c:69-78; see 136-141 where `features_m1` also serves M1 Pro and Max]** (whether it is also active on the plain t8103 is [H]: to be read from the value).
- Value read of `SIQ_CFG_EL1` on M1: `0x3` both in m1n1 and in Darwin **[V: D:system-register-dumps.md:208 and :579]**. If the dump really comes from a base M1, the value 0x3 does not block IRQs there (AICv1 does not use it) [H]. This apparent contradiction with the sentence "0x3 = no IRQ" suggests that the register concerns only AICv2 [H].
- The registrations of the kind `VM_TMR_LR_EL2`/`VM_TMR_FIQ_ENA_EL2` and `HACR_EL2` bit 20 are described: the guest timer, when it fires, sets the "pending" state in a vGIC list-register-like register, and `HACR_EL2[20]` masks it leaving the FIQ "pending" in `VM_TMR_LR` **[V: D:system-registers.md:493-497, 662]**.
- No other public source found that uses or describes `CONVERT_SIQ_TO_IRQ`. A web search with the terms `ACC_OVRD`, `SIQ`, `s3_5_c15_c6_0` returned nothing relevant (only the AIC documentation and discussions about the 2021 Linux patch) **[V: WebSearch]**.

---

## 3. What the "SIQs" are

**We do not know.** What exists:
1. In m1n1, "SIQ" appears in three contexts, and they are not necessarily the same thing:
   - `SIQ_CFG_EL1`, "System **Interrupt** Configuration" (long name in `apple_regs.json`): controls whether a core receives IRQs from the controller (AICv2) **[V: apple_regs.json:227; system-registers.md:737]**.
   - `ACC_OVRD_CONVERT_SIQ_TO_IRQ` (bit 22).
   - `HID1_CONSERVATIVE_SIQ` (bit 44 of HID1) and `EHID20_BLZ_CONSERVATIVE_SIQ` (bit 6 of EHID20) **[V: cpu_regs.h:80, 391]**. Here the name seems to refer to the core's pipeline, **not** to interrupts: the comment in `hurricane_zephyr.c` accompanying `reg_set(HID1, HID1_CONSERVATIVE_SIQ)` says "disable crypto fusion across decode groups" and "Original Name: ARM64_REG_HID1_disAESFuseAcrossGrp" **[V: M:src/chickens/hurricane_zephyr.c:20-26]**, i.e. the m1n1 name `CONSERVATIVE_SIQ` and the XNU name do not coincide. So in these two cases "SIQ" could be a different internal acronym (e.g. issue queue) [H].
2. In the interrupt context, "SIQ" could be a "system" interrupt generated inside the core complex (like the timer, the fast IPIs and the PMU) that the system converts into an IRQ towards the AIC; **but this is a conjecture built on the name**. The only evidence in favour is that the `SIQ_CFG_EL1` register has "System Interrupt" in its name and controls IRQs [H].
3. No source I read defines the acronym.

---

## 4. Other IMP-DEF ways to get a periodic interrupt as an IRQ, with level of evidence

| # | Way | Level of evidence | Notes |
|---|---|---|---|
| 1 | `ACC_OVRD[22]` `CONVERT_SIQ_TO_IRQ` | **Name only**, no use or description (§1). Bit 0 in all M1 dumps | Probability that it converts precisely the timer: low [H] (§6) |
| 2 | **EL02 guest timer with `VM_TMR_FIQ_ENA_EL2`** (bit 0 = V, bit 1 = P, `s3_5_c15_c1_3`) | **Register and semantics verified** **[V: L:irq-apple-aic.c:187-189; D:system-registers.md:499-504; M:cpu_regs.h:749-751]**. Linux uses it to actually mask the guest timer's FIQ. The behaviour "FIQ off, but state preserved in `VM_TMR_LR_EL2`" is described **[V: system-registers.md:493-497]** | It does not produce an IRQ: it **turns off** the FIQ and keeps the state in a list-register-like register. To make it an interrupt in Windows an EL2 would be needed that injects a virtual IRQ or FIQ (`HCR_EL2.VI/VF`), as `hv_update_fiq` does **[V: M:src/hv/hv_exc.c:152-180]**. That is a hypervisor and hence plan B. Useful only if Windows runs at EL2 with EL02 timers [H] |
| 3 | Tests already done by the m1n1 team on `HCR_EL2.TGE`, `HACR_EL2[20]`, `VM_TMR_FIQ_ENA` reading `ISR_EL1` and `VM_TMR_LR` | The script `proxyclient/experiments/timer_test.py` exists **[V: M:proxyclient/experiments/timer_test.py:1-109]** but **we do not know its results**: they are not in the repo | Useful as a model for HW-1 (§5); the results may be in their private documentation [H] |
| 4 | **PMU in AIC mode**: `PMCR0.IMODE = 2` (AIC) and `UPMCR0.IMODE = 2` | The value "2=AIC" is documented **[V: D:system-registers.md:558,622; M:cpu_regs.h:605]**. **It is not known which AIC hwirq** corresponds to it nor whether it works on t8103 | A PMC counter (e.g. cycles) programmed to overflow after N cycles would be a periodic "timer" as IRQ, but it **counts cycles and not time**: with DVFS the frequency changes, so recalibration is needed. Candidate [H] |
| 5 | **Watchdog WD0, AIC IRQ 338** | The Linux driver comment describes the BARK with interrupt, the DT gives `AIC_IRQ 338` **[V: L:drivers/watchdog/apple_wdt.c:22-35; arch/arm64/boot/dts/apple/t8103.dtsi:1228, already in `re/f0-m1-fiq-irq.md` §3]** | Never exercised in Linux. Risk: `RESET_EN` on WD0 reboots into recovery |
| 6 | **AIC software IRQ (`SW_SET`)** and **legacy IPI** | Verified as a mechanism (`re/f0-m1-fiq-irq.md` §3.4 and §4) | Not a timer: it needs an external periodic source (1, 4 or 5) to trigger it |
| 7 | **Copy of the counter in the AIC** at `0x8020/0x8028` (`CNTPCT`) | Documented, but it is only a counter **[V: D:docs/hw/soc/aic.md:27-28]** | No known compare register: it does not generate interrupts |
| 8 | `CYC_OVRD` bits [21:20] FIQ mode and [23:22] IRQ mode | Documented and used by XNU and m1n1 for idle **[V: X:apple_arm64_regs.h:45-48; M:utils.c:269-270]** | Only "disable" (2), they control the wake-up of cores. They do **not** convert FIQ to IRQ [H] |
| 9 | `SIQ_CFG_EL1` (s3_4_c15_c10_4) | Documented: controls whether AICv2 IRQs reach the core **[V: system-registers.md:737]** | It does not concern FIQs ("FIQs unaffected"). It is not a conversion route |
| 10 | Arch timer event stream (`CNTKCTL_EL1.EVNTEN`) | Used by XNU **[V: machine_routines.c:2600-2614]** | Produces an event for WFE, not an interrupt |

Conclusion of the section: **a FIQ→IRQ conversion of the timer is not documented anywhere.** The only two routes with a glimmer of evidence are bit 22 (to be tested) and the PMU in AIC mode (to be tested, cycles and not time).

---

## 5. Protocol HW-1 (issue #20): test of `ACC_OVRD[22]`

### 5.0 Objective
Answer a single question: **with `ACC_OVRD[22] = 1`, does the architectural timer of an M1 core change the way it arrives, from FIQ to IRQ?** The test only reads/writes CPU registers from the proxy, without touching the disk.

### 5.1 Prerequisites
- **Time Machine backup confirmed by the user before starting** (absolute rule). The test does not touch disks or boot, but the starting state requires one, and the environment (debug host with m1n1 and Python) requires installations on the host Mac if it is a Mac. **No downloads or installations until the backup is confirmed.**
- The MacBook Pro M1 (j293) with m1n1 in **proxy mode**. Two documented paths:
  1. Asahi installed with the "UEFI environment only" option and **backdoor proxy mode** (`csrutil disable` + `nvram boot-args=-v` from 1TR on the m1n1 volume, 5-second wait at boot) **[V: D:docs/sw/m1n1-user-guide.md, section "Backdoor proxy mode"; D:docs/sw/tethered-boot.md]**. This change touches only the Asahi volume, not macOS, but requires the user's confirmation before touching boot (project constraint).
  2. "Tethered-only proxy mode" installation of the installer in expert mode **[V: D:tethered-boot.md]**.
- A debug host (Linux, or macOS "less tested") with Python 3.9+, `pyserial`, `construct` and `m1n1` cloned **[V: D:tethered-boot.md]**, connected with a USB-C cable to the Thunderbolt port of the j293. The proxy device is `/dev/ttyACM0` on Linux or `/dev/cu.usbmodemP_01` on macOS; variable `M1N1DEVICE` **[V: D:m1n1-user-guide.md, section "Proxy mode"]**.
- A second channel: a physical serial UART is not needed (the hypervisor is enough) **[V: tethered-boot.md]**, but the USB proxy is enough for this test.
- The Mac under test must be on mains power and have no critical data in session.

### 5.2 Command sequence (m1n1 proxyclient)
Startup: `python3 -i proxyclient/tools/shell.py` (the `shell.py` script exists **[V: listing of proxyclient/tools]**; that it imports `m1n1.setup` is [H], however `proxyclient/experiments/*.py` all use `from m1n1.setup import *`, which exposes `p` (proxy), `u` (utils) and `mon` **[V: M:proxyclient/m1n1/setup.py:12-25]**).

Verified APIs:
- `u.mrs(reg)` / `u.msr(reg, val)` read/write a system register, executed on the proxy core through `p.call`; errors raise `ProxyError("Exception occurred")` because it uses the exception guard **[V: M:proxyclient/m1n1/proxyutils.py:178-231]**. The `call=` parameter selects the level: `None`/`"el2"`, `"el1"`, `"el0"`, `"gl1"`, `"gl2"` **[V: proxyutils.py:108-115]**.
- `u.inst(0xd5033fdf)` executes an instruction (here `isb`) **[V: timer_test.py:19]**.
- `p.read32(addr)`, `p.write32(addr, v)`, `p.set32`, `p.clear32`, `p.mask32` **[V: proxy.py:840-921]**.
- `p.smp_start_secondaries()`, `p.smp_call_sync(cpu, addr, *args)` for the secondary cores **[V: proxy.py:1020-1030]**; how to run code useful for `msr` on another core with these is [H] (a code blob or `u.exec` with `p.smp_call` is needed, not verified).
- Register names used by the scripts: `CNTV_CTL_EL0`, `CNTV_TVAL_EL0`, `CNTP_CTL_EL0`, `CNTP_TVAL_EL0`, `CNTFRQ_EL0`, `ISR_EL1`, `DAIF`, `HCR_EL2` **[V: timer_test.py:16-105]**; `ACC_OVRD_EL1`, `CYC_OVRD_EL1`, `VM_TMR_FIQ_ENA_EL2`, `VM_TMR_LR_EL2`, `SIQ_CFG_EL1` exist in `apple_regs.json` **[V: apple_regs.json:227,243-244,253]**; the import as Python constants `ACC_OVRD_EL1` from `m1n1.sysreg` is confirmed by `hv/__init__.py:663` **[V]**, for `SIQ_CFG_EL1` and `VM_TMR_*` it is **[H]** (same generation mechanism, not seen). When in doubt, use the tuple `(3, 5, 15, 6, 0)` as timer_test.py does (`HV_VTMR_CTL = (3, 5, 15, 1, 3)`) **[V: timer_test.py:8,12]**.

**Step A: baseline, read-only.**
```python
from m1n1.setup import *
acc0 = u.mrs(ACC_OVRD_EL1)
cyc0 = u.mrs(CYC_OVRD_EL1)
siq0 = u.mrs((3,4,15,10,4))         # SIQ_CFG_EL1, [H] only the encoding is verified
ena0 = u.mrs((3,5,15,1,3))          # VM_TMR_FIQ_ENA_EL2
daif0, isr0 = u.mrs(DAIF), u.mrs(ISR_EL1)
freq = u.mrs(CNTFRQ_EL0)
print(hex(acc0), hex(cyc0), hex(siq0), hex(ena0), hex(daif0), hex(isr0), freq)
```
Expected (from the Asahi dumps, M1 Firestorm): `acc0 = 0x180010102001c207`, bit 22 = 0 **[V: system-register-dumps.md:239]**, `siq0 = 0x3` **[V: :208]**, `CNTFRQ = 24000000` **[V: dev-quickstart.md:670; pe_identify_machine.c:53]**. Record all values: they are needed for restoring. If `acc0` already has bit 22 set to 1, **stop** and report.

**Step B: control (bit 22 = 0), virtual timer.** So as not to be interrupted by the proxy, mask everything, as `test_hv_timers` does **[V: timer_test.py:34]**.
```python
u.msr(DAIF, 0x3c0)            # D A I F masked
def try_timer(ctl, tval, ms=100):
    u.inst(0xd5033fdf)        # isb
    u.msr(ctl, 0)
    u.msr(tval, int(freq * ms / 1000))
    u.msr(ctl, 1)             # ENABLE=1, IMASK=0
    out = []
    for i in range(6):
        time.sleep(0.05)
        out.append((hex(u.mrs(ISR_EL1)), hex(u.mrs(ctl))))   # ISR bit7=I, bit6=F; CTL bit2=ISTATUS
    u.msr(ctl, 3)             # IMASK|ENABLE, then turn off
    u.msr(ctl, 0)
    return out
print(try_timer(CNTV_CTL_EL0, CNTV_TVAL_EL0))
print(try_timer(CNTP_CTL_EL0, CNTP_TVAL_EL0))
```
Expected: after 100 ms `CTL.ISTATUS=1` and `ISR_EL1.F=1` (bit 6), `I=0`, because the timer is FIQ (**[H]**, deduction from §2; `ISR_EL1` as timer_test.py does **[V]**; the meaning of the I/F bits of `ISR_EL1` is standard ARMv8 architecture [H, ARM ARM]). **This is the negative control**: if `I=1` already appears here, the FIQ model for the EL0 timer with the current `HCR_EL2` is not as expected and the reason must be understood first. It must also be repeated reading `p.read32(AIC + 0x4200 + ...)` (state of the HW lines, `AIC_HW_STATE` at `0x23b100000 + 0x4200`) **[V: aic_test.py:14,25; D:aic.md "HW_IRQ_MON"]**, to see whether an AIC line changes during the test (expected: none). The exact meaning of each bit is unknown [H]: compare only before/after.

**Step C: writing bit 22.**
```python
u.msr(ACC_OVRD_EL1, acc0 | (1 << 22)); u.inst(0xd5033fdf)
print(hex(u.mrs(ACC_OVRD_EL1)))        # did the bit keep its value?
print(try_timer(CNTV_CTL_EL0, CNTV_TVAL_EL0))
print(try_timer(CNTP_CTL_EL0, CNTP_TVAL_EL0))
u.msr(ACC_OVRD_EL1, acc0); u.inst(0xd5033fdf)       # ALWAYS restore
assert u.mrs(ACC_OVRD_EL1) == acc0
```
Use `try/finally` for the restore. Possible outcomes:
- The write raises `ProxyError`: the register is locked or nonexistent in this context (`apple_sysregs_unlocked` is `true` for M1 in m1n1 **[V: chickens.c:72, 46-62]**, so this is unexpected) [H].
- The readback gives bit 22 at 0: the bit is not writable (RAZ/WI or hardware-managed). "Failure" outcome for the lead.

**Step D: variations (only if C is not enough).**
1. Repeat C after `u.msr((3,5,15,1,3), 0)` and `(...,3)` to rule out the effect of the IMP-DEF mask.
2. Repeat C with `SIQ_CFG_EL1` at 0x2 instead of 0x3 (`m1n1 chickens.c` writes 2 **[V: chickens.c:231]**) to see whether the conversion requires the core's IRQ path to be enabled. Restore to `siq0`.
3. Try the other cores: start the secondaries (`p.smp_start_secondaries()`), repeat the test on one E core and one P core with `p.smp_call_sync` (**[H]** on how to execute the `msr`s: it requires a code blob loaded in memory; do this only if step C on the boot core gives a positive or ambiguous outcome). The bit might be per cluster [H].
4. To see a **true** IRQ and not just `ISR_EL1`, the next step is to unmask only `DAIF.I` (`u.msr(DAIF, 0x340)`: I free, F masked; value to be recomputed carefully) and observe whether m1n1's `exc_irq` runs and which AIC event it reads (AIC `EVENT` at `+0x2004`, type bits 23:16, number 15:0 **[V: re/f0-m1-fiq-irq.md §4.1]**). What m1n1's IRQ handler does with an unknown event is **[H]**: it might print and continue, or hang. Run only after a positive outcome on `ISR_EL1`.

### 5.3 What to observe
- For each trial: `ISR_EL1` (bit 7 = IRQ pending, bit 6 = FIQ pending), `CTL.ISTATUS` (bit 2), values of `CNTVCT_EL0`.
- AIC state before/after: `AIC_HW_STATE` (`+0x4200`), `AIC_SW_GEN` (`+0x4000`), masks (`+0x4100`), and `EVENT` (`+0x2004`) only in step D4 (the read is an acknowledge) **[V: aic_regs.h already cited in `re/f0-m1-fiq-irq.md`; aic_test.py:14-31]**. One can use `mon.add(AIC + 0x4000, 0x200)` and `mon.poll()` as aic_test.py does **[V: aic_test.py:69-80]**.
- IMP-DEF registers: `ACC_OVRD`, `VM_TMR_FIQ_ENA_EL2`, `VM_TMR_LR_EL2` (`s3_5_c15_c1_2`: at EL2 with the guest timer), `IPI_SR_EL1`, `PMCR0` to be sure that they are not the source of FIQs.
- Any exceptions on the proxy (`p.get_exc_count()`) **[V: proxy.py:802]**.

### 5.4 Criteria
- **Full success:** with bit 22 = 1, the expired timer raises `ISR_EL1.I` (bit 7) and **not** `ISR_EL1.F`; in step D4 an IRQ event arrives in the AIC with a type and number that can be recorded (the timer's hwirq). The next question follows: is it per core? does it apply to EL0 phys and virt? does it apply to EL02?
- **Partial success:** `I` and `F` both high, or `I` high in only one of the two timers: the bit does something but is not enough; document it.
- **Failure:** `ISR_EL1` identical to the control (only `F`), or the bit cannot be written, or the write raises an exception. Outcome to be reported as "bit 22 does not convert the EL0 timer on M1", closing lead (a) of `re/f0-m1-fiq-irq.md`. Then move on to PMU/AIC (§4, route 4) or to the WDT.
- **Anomaly:** Mac hang, FIQ storm, loss of the proxy: interrupt the test, restore by rebooting (§5.5), note the step.

### 5.5 Safe restore
- Before writing: save the values read in step A (`acc0`, `siq0`, `ena0`, `cyc0`) also to a file on the host.
- Each trial is closed by restoring bit 22 (`u.msr(ACC_OVRD_EL1, acc0)`) in a `finally`, and the timers turned off (`CNTV_CTL_EL0 = 0`, `CNTP_CTL_EL0 = 0`). `DAIF` put back to `daif0`.
- The test writes nothing to disk, NVMe, NVRAM, iBoot or 1TR, apart from the possible preliminary enabling of the backdoor proxy mode (§5.1), which is a separate operation with user confirmation.
- **Do the registers reset on reboot?** Not verified in any source. Deduction: they are core system registers, cleared or returned to their reset values at every core reset, and m1n1 never writes bit 22 or `ACC_OVRD` in normal startup **[V: grep, §1.2]**; the m1n1 and Darwin dumps coincide, which suggests that the value at startup is decided by reset/iBoot and not by macOS **[V as data, the rest is H]**. Keep open the possibility that a "warm" reboot (e.g. `proxyclient/tools/reboot.py` **[V: tools listing]**) does not reset a cluster register in an always-on power domain; to be safe do a complete shutdown (hold the power button) and power on again, then re-read `ACC_OVRD` from step A and compare it with `acc0` [H].
- If the Mac does not respond: forced shutdown with the power button (macOS and recovery are not touched by the test).

### 5.6 Risks
- Low for the disk; **medium** for stability: the bit sits in a cluster power-management register (CPM, SRM, DVFM, PSW). An unwanted change of a nearby bit would be serious, so the write is always `acc0 | (1 << 22)` read-modify-write, never a hand-built value.
- We do not know whether the bit has side effects on core wake-up, on IPIs or on deep sleep [H]. The test does not use `cpu_sleep` or deep WFIs.

---

## 6. How promising is HW-1

Judgment: **unlikely to work on the timer, but cheap and low-risk, hence worth doing**.

In favour:
- The bit's name explicitly says "convert to IRQ" and the register exists on M1: it looks like a function that Apple may have provided (for example for SoCs where the core's IRQ path is used differently) **[V for the name, H for the meaning]**.
- The new Apple generic timer (`HAS_APPLE_GENERIC_TIMER`, `S3_x` registers, 1 GHz) shows that Apple changes the timer path in new SoCs **[V: X:H15.h:49; tests/apple_generic_timer.c]**; this says nothing about an IRQ option [H].

Against:
- Zero uses, zero descriptions, bit at 0 also in macOS (which uses the timer as FIQ) **[V]**.
- The register is dominated by power-management bits; "SIQ" is undefined (§3). It may be a cluster-internal signal tied to energy management (for example a wake-up signal) and not the timer [H].
- The Asahi documentation says that FIQs "are part of the core complexes" and therefore do not pass through the block that handles IRQs **[V: system-registers.md:737]**: if "SIQ" is an interrupt that goes towards the IRQ block, the timer is excluded by construction [H].
- Estimated probability that the bit converts the EL0 timer: on the order of **10%**, my judgment, not measured [H].

The value of the test is also negative: closing lead `a` with a verified fact is worth as much as finding it. All commands are reversible within the session. In parallel, the test of the PMU in AIC mode (§4 route 4) and of the WDT IRQ 338 (A8/A10) remain the alternatives with the best chance of yielding a periodic interrupt as an IRQ, at the cost of a non-standard timer HAL.
