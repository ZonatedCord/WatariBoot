# M3a day 1: E-HW0 read-only measurements on the real M1 (j293)

- **Date:** 2026-11-10. **Who:** Marco (target) + Ciani (host), guided by Claude on the host.
- **Target:** MacBook Pro 13" M1 2020, j293ap / MacBookPro17,1, t8103 (chip 0x8103, board 0x24), 8 GB RAM (`mem_size_act` 0x200000000). macOS 26.5.1 (25F80), system firmware mBoot-18000.120.36.
- **Asahi:** installer v0.9.2, expert mode, OS "Tethered boot (m1n1, for development)", stub boot firmware 13.5 (iBoot-8422.141.2), 2.5 GB stub; macOS container shrunk 245.11 -> 230.00 GB, 15.11 GB freed (12.6 GB still unpartitioned).
- **m1n1:** v1.6.1 (installed by the installer), running at EL2 in proxy mode, no payload. **proxyclient:** m1n1 `main` source zip of 2026-11-10, sha256 `5492617470dc3072324e78572b0a293007168e260b10b41c3949c27e183a8987`, Python 3.12.2 venv.
- **Host:** MacBook Pro M4 (Mac16,1), macOS 27.2.1. **Cable:** USB-C to USB-C data cable, target side on the **left port nearest the hinge**; host saw `m1n1 uartproxy v1.6.1`, devices `/dev/cu.usbmodem<serial>1` (proxy) and `<serial>3` (not `usbmodemP_01/P_03` as in the Asahi docs).
- **Method:** `tools/m3a/ehw0.py`, `tools/m3a/ehw0b.py` (read-only; they do **not** import `m1n1.setup`, which writes the PMU, see the procedure). System registers read with `u.mrs` (one `mrs` + `ret` run by m1n1 from its RAM heap); memory reads with `p.read32` only at AIC offsets listed below; AIC `EVENT` (0x2004) never read; no write, no SMP start, no chainload.
- **Caveat:** every system register value is the boot core's (cpu0, an Icestorm E core) **as handed over by m1n1 v1.6.1**, not a reset value.

All values below: **[verified: measured on j293 on 2026-11-10, m1n1 v1.6.1]** unless marked otherwise.

## Row 1: AIC
- ADT `/arm-io/aic`, `compatible = aic,1`; `reg[0] = 0x23b100000` size **0xc000**, `reg[1] = 0x23b108000` size 0x1000. m1n1 log: "AIC: Version 1 @ 0x23b100000".
- `INFO` (+0x0004) = `0x000a0380`: **NR_IRQ = 896** (bits [15:0] = 0x380); bits [31:16] = 0xa (meaning not known **[hypothesis]**: maybe a die/version field).
- Matches our code: base **yes**, NR_IRQ 896 **yes** (Q-D4 settled). Window: **no**, `el2/include/wel2_aic.h` says 0x8000; the ADT lists 0xc000 plus a second 4 KB range at +0x8000. Whatever is at 0x23b108000 must be mapped/trapped if the layer touches it.

## Row 2: core identity
- Boot core: `MPIDR_EL1` = `0x80000000` (Aff0 = 0, Aff1 = 0, U = 0), AIC `WHOAMI` (+0x2000) = 0.
- ADT `/cpus`: cpu0-3 `apple,icestorm`, `cluster-type E`, `cluster-id 0`, `reg` 0..3; cpu4-7 `apple,firestorm`, `cluster-type P`, `cluster-id 1`, `reg` **0x100..0x103**; `cpu-id` 0..7.
- **Every core, measured** (`tools/m3a/ehw0c.py`, run after Marco and Ciani both agreed to start the secondaries; m1n1 started all 7: "Starting CPU 4 (0:1:0)... Started."). On each core: `mrs MPIDR_EL1`, `mrs MIDR_EL1`, one `ldr` of AIC `WHOAMI`:

| core | MPIDR_EL1 | Aff2.Aff1.Aff0 | MIDR_EL1 | WHOAMI |
|---|---|---|---|---|
| cpu0-3 (Icestorm, E) | `0x80000000`..`0x80000003` | 0.0.0 .. 0.0.3 | `0x611f0221` (part 0x22) | 0, 1, 2, 3 |
| cpu4-7 (Firestorm, P) | `0x80010100`..`0x80010103` | **1.1.0 .. 1.1.3** | `0x611f0231` (part 0x23) | 4, 5, 6, 7 |

- The P cores have **Aff2 = 1 and Aff1 = 1** (the ADT `reg` 0x100..0x103 shows only Aff1). This matches `tools/acpigen/src/t8103_model.c` (0x10100..0x10103, taken from Linux `t8103.dtsi`).
- WHOAMI = ADT `cpu-id` = 0..7, E cores first: matches the `cpu_of_vcpu` assumption E0-3 = 0-3, P0-3 = 4-7 **yes** (#55, R-D5 settled).
- **Consequence for the layer:** `el2/boot/start.S` `secondary_entry` picks the EL2 stack slot from `MPIDR.Aff0` (comment: "real hardware needs a proper cpu index"). On the M1 E0 and P0 both have Aff0 = 0, so two cores would share a stack. Fix before the first M1 boot of the layer: index = Aff0 + 4 x Aff1 (or WHOAMI), bounded by 8. Not changed today: no toolchain on the host Mac to build and test it. The C side already uses `cpu_from_mpidr()` (full affinity), so only the assembly needs it.

## Row 3: ICH_VTR_EL2
- `0x90580007`: ListRegs = **8**, PREbits = **5**, PRIbits = **5**, **SEIS = 1**, TDS = 1. The read did not fault: the M1 implements the GICv3 virtual CPU interface registers at EL2.
- Matches our code: SEIS = 1 **yes** (F-19, #52: the TRAP_EMULATE path is needed). PRIbits/PREbits 5 are inside the 5..7 the layer supports (#60/#64). 8 LRs: the layer's LR overflow handling (#54) applies.

## Row 4: HCR_EL2
- `0x0000032488000038`: **E2H = 1**, TGE = 1 (m1n1 runs in the VHE host regime). The "write 0 and read back" stuck-bit test is a write: day 2.
- Matches: `wel2.e2h=1` default **yes**.

## Row 5: ID registers
- `ID_AA64MMFR0_EL1` = `0x000012120f100001`: **PARange = 1 (36-bit PA)**, TGran4 = **0 (4 KB supported)**, TGran16 = 1 (supported), TGran64 = 0xf (not supported), TGran4_2 = 2, TGran16_2 = 2, TGran64_2 = 1 (stage 2: 4 KB and 16 KB supported, 64 KB not).
- `ID_AA64MMFR1_EL1` = `0x0000000011212100`: VH = 1.
- `ID_AA64PFR0_EL1` = `0x1101000010110111`.
- Matches our code: PARange 36 bits **yes** (the boot log line already says "36-bit"). TGran16 **yes** (#58). **No** for `el2/boot/README.md`, which said "On the M1 `TGran4 = 0b1111`: 16 KB is the only choice": the M1 CPU supports 4 KB at stage 1 and stage 2. The 16 KB default stays (the DART/IOMMU pages are 16 KB **[hypothesis]**, and the code is tested that way); the 4 KB fallback is not needed on this chip. README corrected.

## Row 6: CPTR_EL2, CNTHCTL_EL2
- `CPTR_EL2` = `0x300000` (E2H = 1 layout: FPEN [21:20] = 0b11, nothing trapped). `CNTHCTL_EL2` = `0`.
- Matches: the layer writes FPEN = 0b11 with E2H = 1 **yes**. CNTHCTL_EL2 = 0 under E2H = 1 means EL0 physical/virtual counter and timer accesses are trapped by the host regime fields (EL0PCTEN/EL0VCTEN etc. = 0) **[hypothesis: decode not checked field by field]**; the layer writes its own value.

## Row 7: physical map around 0xFFFFC000-0xFFFFFFFF (#74, Q-D2)
- No ADT node `reg` (translated) overlaps `[0xFFFFC000, 0x100000000)`: **0 overlaps**. The addresses themselves were not read.
- `/chosen/memory-map` holds only firmware/kernel carve-outs (`MemoryMapReserved-0..15`, `TrustCache`, `DeviceTree`, `SEPFW`, `preoslog`, `BootArgs`, `BootKC-*`, `AuxKC-*`); all values are in the full ADT dump (kept on the host, not committed).
- **Apple's PCIe MSI doorbell is 0xFFFFF000:** every PCIe node has `msi-address = 4294963200` = `0xFFFFF000` (see row 12). This is the address the design already uses for the emulated GICv2m frame (`wel2_v2m.h`: the device MSI is turned into an AIC edge by Apple PCIe and never reaches CPU-visible memory). Consequence for #74: the 16 KB stage-2 hole `0xFFFFC000-0xFFFFFFFF` overlaps no ADT device, so emulating it is free of neighbours **[verified for ADT-listed devices only]**.

## Row 8: CNTFRQ_EL0
- `0x016e3600` = **24 000 000 Hz**. Matches **yes**.

## Row 9: MIDR_EL1
- Boot core (Icestorm, E): `0x611f0221` = implementer 0x61 (Apple), variant 1, part 0x22, revision 1 (m1n1: "CPU part: 0x22 rev: 0x11").
- Firestorm (P) cores: `0x611f0231` = Apple, variant 1, part **0x23**, revision 1 (measured on cpu4-7, see row 2).

## Row 10: ANS
- `/arm-io/ans`, `compatible iop,ascwrap-v4`, `interrupts` = **584, 583, 586, 585, 590**. Matches `intr_map.c` (590 level, 583-586 mailbox denied) **yes**. Its `reg` entries are bus addresses (not translated by the script).

## Row 11: USB
- `usb-drd0` (`usb-drd,t8103`): **777**, 778, 779, 780, 736. `usb-drd1`: **857**, 858, 859, 860, 816. `atc-phy0/1` have no interrupts. Matches DWC3 777/857 **yes**; the other four lines per controller are not in our map yet.

## Row 12: PCIe
- `/arm-io/apcie` (`apcie,t8103`): `interrupts` **695, 698, 701**; `msi-address 0xFFFFF000`, `msi-vector-offset` **704**, `#msi-vectors` **32**, `msi-parent-controller` 112; 21 `reg` ranges from 0x690000000 (ECAM, 256 MB), 0x680000000.., 0x681000000/0x682000000/0x683000000 (ports), one at 0x23d2bc000.
- Thunderbolt controllers: `apciec0` IRQ 770, MSI offset 738, 32 vectors, ECAM 0x3b0000000; `apciec1` IRQ 850, MSI offset 818, ECAM 0x530000000; `apciec*-piodma` IRQ 772 / 852.
- Matches: ports 695/698/701 **yes**; MSI pool 704..735 (`PRD.md` FR-D13) **yes**; doorbell 0xFFFFF000 **yes** (design assumption confirmed).

## Row 13: UART
- `uart0` (`uart-1,samsung`): `reg` **0x235200000** size 0x4000, IRQ **605**, clock gate 54. `uart2`: 0x235208000, IRQ 607, clock gate 56. m1n1 itself uses a dockchannel UART at 0x23d128000.
- Matches the Linux t8103 DT uart0 address **yes**.

## Row 14: safe AIC reads
- `HW_IRQ_MON` (+0x4200) = 0; `MASK_SET[0]` (+0x4100) = `0xffffffff` (sources 0-31 masked); `TARGET_CPU[0]` (+0x3000) = 1 (source 0 -> core 0); `TARGET_CPU[1]` (+0x3004) = 0. Meanings remain as in `wel2_aic.h` **[hypothesis]**.

## Also seen in the m1n1 boot log
- RAM base 0x800000000, top of normal RAM 0x9df610000; TZ0/TZ1/TZ3 carve-outs unmapped by m1n1; framebuffer 2560x1600 30 bpp at 0x9e2570000; watchdogs at 0x23d2b0000 / 0x23d2b8008 (disabled by m1n1); `Boot policy: sip0 = 0`; m1n1 enabled the PMGR parents of ANS2, SIO_ADMA, DEBUG on its own (not our action).

## Hypotheses settled / still open
- Settled: **Q-D4** (NR_IRQ 896), **#52** (SEIS = 1 on hardware), **#55 / R-D5** (MPIDR of all 8 cores, WHOAMI 0..7 = cpu-id, MIDR E 0x22 / P 0x23), **#58** (16 KB supported; 4 KB too), **#59** (AICv1 at 0x23b100000), **#60** (E2H = 1, CPTR_EL2 layout), **#74 / Q-D2** (no ADT device in the 16 KB hole; Apple doorbell at 0xFFFFF000), interrupt numbers for ANS, DWC3, PCIe ports, PCIe MSI pool 704..735, UART0.
- Corrected: AIC window size (0xc000 + 0x1000 at +0x8000, not 0x8000); TGran4 (supported).
- New to-do from the measurements: the `start.S` stack slot by Aff0 collides on the M1 (row 2).
- Still open: E2H stuck-bit test, AIC edge/latch behaviour (Q-D5) and anything else that needs writes: **day 2**. Apple PCIe MSI data semantics (#68, E-HW1).

## Day 2 experiments (same day, writes, both people agreed)
Volatile CPU/AIC state only; m1n1 still running afterwards. Scripts in `tools/m3a/`.

### E2H stuck-bit test (#60): `e2h_test.py`
- On cpu7 (P core), one instruction block: read `HCR_EL2`, write it back with E2H (bit 34) cleared, `isb`, read, restore the original, `isb`.
- Before `0x0000032488000038`, after writing E2H = 0 **`0x0000032488000038`**, after restore the same: **E2H is stuck at 1** (the write of 0 is ignored). **[verified: measured on j293 2026-11-10, m1n1 v1.6.1]** The layer must run with E2H = 1 on the M1; `wel2.e2h=0` is only usable in QEMU.

### AIC software-trigger and mask behaviour (Q-D5 family): `aic_qd5.py`
- Test IRQ **891** (the highest number below NR_IRQ that no ADT node lists; 287 distinct numbers are in use), routed only to **core 7**, because core 0 runs the m1n1 proxy with IRQs unmasked (`DAIF = 0x0`; cores 1-7 have `DAIF = 0xc0`). EVENT is read on core 7 (it is a per-core view). Initial state: `TARGET_CPU[891] = 0`, masked, SW clear; four EVENT reads before the test: all 0.

| step | action | result |
|---|---|---|
| T1 | masked, no SW | EVENT 0 |
| T2 | `SW_SET` while masked | SW bit reads 1, still masked, EVENT **0**: a masked source does not deliver |
| T3 | `MASK_CLR` | EVENT **`0x0001037b`** = die 0, type 1 (HW IRQ), number 891 |
| T4 | after that EVENT read | **masked again** (the read masks the source); SW bit **still 1** |
| T5 | EVENT again | 0 |
| T6 | `MASK_CLR` again, SW still set | EVENT `0x0001037b` again: SW_SET is **level-like**, it re-fires until cleared |
| T7 | `SW_CLR` | SW bit 0 |
| T8 | `MASK_CLR` with nothing pending | EVENT **0**: no spurious event |
| end | restore | `TARGET_CPU = 0`, masked, SW clear |

- Settled: EVENT layout and auto-mask on read **[verified]**; EVENT does not clear SW_SET; MASK_SET / SW_SET reads return the current state; no spurious zero event in this sequence. Consequence for `pirq_aic.c`: a `sw_trigger` must be followed by `SW_CLR` when the event is taken (or at EOI), or the source fires again at every unmask.
- Still open: whether a **hardware edge** pulse is latched while the source is masked (needs a real edge source, e.g. a PCIe MSI: E-HW1, #68).

### vGIC list-register delivery (Plan B "hw" cpuif mode): `vgic_test.py` (cpu7), `vgic_test0.py` (core 0), `el1_ctrl.py`
- At EL2 the virtual CPU interface accepts the set-up: `ICC_SRE_EL2 = 0xf` before (SRE and Enable already 1), `ICH_VMCR_EL2` written `0xff000002` (VPMR 0xff, VENG1), `ICH_LR0_EL2 = 0x50a000000000002a` (pending, group 1, priority 0xa0, vINTID 42) read back as written, `ICH_HCR_EL2.En = 1`, `ICH_MISR_EL2 = 0`. Initial `ICH_VMCR_EL2 = 0xf84c0008`, `ICH_ELRSR_EL2 = 0xff` (8 empty LRs, consistent with ListRegs = 8).
- **Control:** `el1_call` on core 0 without the vGIC armed works (`CurrentEL = 1`, `MIDR_EL1` read at EL1).
- **With the LR armed, the first EL1 entry hangs m1n1** (no reply, power cycle needed): once on cpu7 (`smp_call_sync_el1`, DAIF 0xc0 at EL2 there) and once on core 0 (`el1_call`). The restore did not run either time; a reboot clears it.
- **[hypothesis]** On core 0 m1n1 enters EL1 with the PSTATE it captured at EL2 (`exception_asm.S` `el1_call`: DAIF taken from the caller, core 0 runs with DAIF = 0), so the pending virtual IRQ would be taken at once through `VBAR_EL1`, which m1n1 does not set up for this: a hang that would mean the LR **was delivered**. The cpu7 hang, with IRQs masked, is not explained by this. Not settled.
- **Next:** a small EL1 test payload with its own vector table (take the IRQ, read `ICC_IAR1_EL1`, EOI, return a value), like the layer's QEMU payloads, built on a machine with the toolchain. Until then the "hw" mode on the M1 is unproven; the "trap" mode does not depend on it.
- **Third attempt, `vgic_masked.py` (core 0), with the full m1n1 console captured:** EL2 `DAIF` set to 0xc0 first (verified); EL1 read of `ISR_EL1` before arming = 0 (EL1 entry works); after arming LR0 (vINTID 42) the next EL1 entry printed **`EL1 Exception: 0x15`**, then m1n1 hung. In m1n1's EL1 vector table (`_el1_vectors_start`, `hvc 0x10 + index`) 0x15 is **IRQ, current EL with SP_ELx**: with `HCR_EL2.IMO = 1` an IRQ taken at EL1 can only be a virtual one, so **the list-register interrupt was delivered to EL1** **[verified: measured on j293 2026-11-10; inference from the vector index]**. m1n1 has no EL1 IRQ handler, treats it as an unhandled exception and stops answering.
- Still open: why the IRQ was taken although EL1 should have been entered with I masked (m1n1 `el1_call` builds SPSR from the captured PSTATE) **[hypothesis: the captured value is not what we think]**; IAR value, LR state after ack/EOI. These need an EL1 payload with its own vectors.
