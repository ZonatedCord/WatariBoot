# M0-A: register and constant check for Watari EL2 against the Arm specifications

- Date: 2026-10-15 · Task: M0-A · Subject: every `[hypothesis]` architectural value in [`docs/design/watari-el2/INTERFACES.md`](../docs/design/watari-el2/INTERFACES.md).
- Method: the four official PDFs were downloaded to `~/WatariBoot-specs/` (outside the repository, see `docs/INSTALLED.md`) and converted to text with `pdftotext -layout` (already installed on the Mac; nothing was installed). Each value was located in the register description and, for trap behavior, in the per-register access pseudocode.
- Documents (page numbers are the printed page numbers of the PDF):
  - **ARM ARM**: Arm Architecture Reference Manual for A-profile, DDI 0487 **M.d** (17150 PDF pages; sections `D24.x`).
  - **GIC**: Arm GIC Architecture Specification v3/v4, IHI 0069 **H.b** (chapter 12 "Programmers' Model").
  - **PSCI**: Arm Power State Coordination Interface, DEN0022 **F.b** (PSCI 1.3).
  - **SMCCC**: SMC Calling Convention, DEN0028 **1.7 H**.
- Status legend: **confirmed** = the draft value matches the spec; **corrected** = the draft was wrong or incomplete and INTERFACES.md was changed; **unverified** = not an Arm architectural fact (or the spec is silent), left as `[hypothesis]`.
- Where only a section is given, the register description starts on the page of the section header and the exact page was not recorded.

## 1. GICv3 distributor (GICD), single security state (`DS=1`)

| # | Item | Value in INTERFACES.md (draft) | Value in the spec | Source | Status |
|---|---|---|---|---|---|
| 1 | `GICD_CTLR` layout with `DS=1` | bit0 EnableGrp0, bit1 EnableGrp1, bit4 ARE, bit6 DS, bit7 E1NWF, bit31 RWP | Single-security view: same bits; bit8 `nASSGIreq` (GICv4.1 only), `DS` RAO/WI, `ARE` RAO/WI if no GICv2 compatibility, `E1NWF` programmable or RAZ/WI (IMPLEMENTATION DEFINED) | GIC §12.9.4, pp.12-547/548 | confirmed |
| 2 | `GICD_CTLR.RWP` always 0 | "RWP always 0" | RWP is read-only; 0 means "no register write in progress", so a constant 0 is conformant | GIC §12.9.4, p.12-547 | confirmed |
| 3 | `GICD_TYPER` field positions | ITLines 4:0, CPUNumber 7:5, SecurityExtn 10, MBIS 16, LPIS 17, IDbits 23:19, A3V 24, No1N 25, RSS 26 | Same; also ESPI 8, NMI 9, num_LPIs 15:11, DVIS 18, ESPI_range 31:27. `SecurityExtn` is RAZ when `DS=1` | GIC §12.9.38, pp.12-618..621 | confirmed |
| 4 | `GICD_TYPER` expected value | `0x0178_001D` (`IDbits=15`) | `IDbits` = INTID bits minus one; a GIC without LPIs must implement 5 to 10 INTID bits (13 for GICv3.1), so the conformant field is 9 and the value is `0x0148_001D` (ITLines 0x1D, IDbits 9<<19, A3V 1<<24) | GIC §2.2.2, p.2-37; §12.9.38 p.12-619 | **corrected** (QEMU may differ: the oracle decides; see note A) |
| 5 | `ITLinesNumber=29` gives 960 INTIDs | max INTID 959 | max SPI INTID = 32·(N+1)−1 = 959; INTIDs 1020-1023 special | GIC §12.9.38 p.12-621; Table 2-1 p.2-35 | confirmed |
| 6 | `GICD_IROUTER<n>` offset | `0x6000-0x7FD8` | offset = 0x6000 + 8·n, n = 32..1019, so the range is `0x6100-0x7FD8`; n < 32 reserved | GIC Table 12-25 p.12-534; §12.9.22 p.12-584 | **corrected** |
| 7 | `GICD_IROUTER` fields | `Aff3.Aff2.Aff1.Aff0`, bit31 IRM | Aff0 [7:0], Aff1 [15:8], Aff2 [23:16], **Aff3 [39:32]**, IRM bit31 (1 = any participating PE), bits 30:24 and 63:40 RES0 | GIC §12.9.22 p.12-584 | confirmed (Aff3 position added) |
| 8 | Distributor offsets (SETSPI_NSR 0x40, CLRSPI_NSR 0x48, IGROUPR 0x80, ISENABLER 0x100, ICENABLER 0x180, ISPENDR 0x200, ICPENDR 0x280, ISACTIVER 0x300, ICACTIVER 0x380, IPRIORITYR 0x400, ITARGETSR 0x800, ICFGR 0xC00, IGRPMODR 0xD00, NSACR 0xE00, SGIR 0xF00, CPENDSGIR 0xF10, SPENDSGIR 0xF20, TYPER2 0x0C, STATUSR 0x10) | as listed | identical | GIC Table 12-25 pp.12-533/534 | confirmed |
| 9 | ID window `0xFFD0-0xFFFC` | "static values, `PIDR2.ArchRev=3`" for PIDR4..PIDR3, CIDR0..3 | Only `GICD_PIDR2` (0xFFE8) is architectural; `ArchRev` bits 7:4: 3 = GICv3, 4 = GICv4. All other registers in the window are IMPLEMENTATION DEFINED (CoreLink scheme recommended) | GIC §12.1.13 Tables 12-15, 12-18, pp.12-209/210 | **corrected** (ArchRev=3 confirmed; the other registers are not architectural) |
| 10 | `IPRIORITYR` implements 5 bits (32 levels) | top 5 bits | 4 to 8 priority bits are allowed; 5 bits means values in steps of 8, unimplemented low bits RES0; the virtual interface must implement at least 5 | GIC §4.8 Table 4-9 p.4-71; ICH_VTR_EL2.PRIbits p.12-365 | confirmed |

## 2. Redistributor (GICR) and INTIDs

| # | Item | Draft | Spec | Source | Status |
|---|---|---|---|---|---|
| 11 | `GICR_TYPER` layout | `Affinity_Value` in 63:32, `Processor_Number`, `Last`, PLPIS/VLPIS/DirectLPI = 0 | Affinity_Value [63:32] (Aff3 63:56 ... Aff0 39:32), Processor_Number [23:8], Last bit4, DirectLPI bit3, Dirty bit2, VLPIS bit1 (RES0 in GICv3), PLPIS bit0; `Last` = highest-numbered Redistributor of a contiguous series | GIC §12.11.37, pp.12-711..714 | confirmed |
| 12 | `GICR_WAKER` | ProcessorSleep bit1, ChildrenAsleep bit2, reset both 1 | Same; ChildrenAsleep is RO; software sets Sleep=0 and polls ChildrenAsleep=0; changing Sleep 1 to 0 when ChildrenAsleep is not 1 is UNPREDICTABLE; bits 31 and 0 IMPLEMENTATION DEFINED | GIC §12.11.42, pp.12-728/729 | confirmed |
| 13 | `GICR_CTLR` | EnableLPIs RAZ/WI, RWP 0, UWP 0 | EnableLPIs bit0 (RES0 when `PLPIS=0`), RWP **bit3**, UWP **bit31**, DPG0/1NS/1S bits 24/25/26 (RAZ/WI when `DPGS=0`) | GIC §12.11.2, pp.12-641..644 | confirmed (bit positions added) |
| 14 | Frame layout and offsets | RD_base + SGI_base (+0x10000); CTLR 0x00, IIDR 0x04, TYPER 0x08, STATUSR 0x10, WAKER 0x14; SGI_base: IGROUPR0 0x80, ISENABLER0 0x100, ICENABLER0 0x180, ISPENDR0 0x200, ICPENDR0 0x280, ISACTIVER0 0x300, ICACTIVER0 0x380, IPRIORITYR0..7 0x400-0x41C, ICFGR0/1 0xC00/0xC04, IGRPMODR0 0xD00, NSACR 0xE00 | identical; two contiguous 64 KB frames (GICv3: no VLPI frames) | GIC §12.10 Tables 12-27, 12-29, pp.12-634..637 | confirmed |
| 15 | `GICR_ICFGR0` "SGI always edge (RAO)" | RAO | each 2-bit field: 0b00 level, 0b10 edge; SGIs are always edge (read 0b10); for PPIs bit 2x is RES0 | GIC §12.11.7/12.11.8, pp.12-653/654 | confirmed (refined: reads 0xAAAAAAAA for SGIs) |
| 16 | INTID partition | SGI 0-15, PPI 16-31, SPI 32-927 used, 1023 spurious | SGI 0-15, PPI 16-31, SPI 32-1019, special 1020-1023 (1023 = no pending interrupt) | GIC Table 2-1 p.2-35; §2.2.1 p.2-36 | confirmed |

## 3. Virtual CPU interface (ICH_* / ICC_*) and HCR_EL2

| # | Item | Draft | Spec | Source | Status |
|---|---|---|---|---|---|
| 17 | `ICH_HCR_EL2` bits | En, TALL0, TALL1, TC, TDIR (positions not stated) | En 0, UIE 1, LRENPIE 2, NPIE 3, VGrp0EIE 4, VGrp0DIE 5, VGrp1EIE 6, VGrp1DIE 7, TC 10, TALL0 11, TALL1 12, TSEI 13, TDIR 14, DVIM 15, EOIcount 31:27; encoding `S3_4_C12_C11_0` | GIC §12.4.5, pp.12-348..352 | confirmed |
| 18 | `TDIR` availability | `TDIR=1` always | `TDIR` exists only with FEAT_GICv3_TDIR (`ICH_VTR_EL2.TDS=1`); otherwise RES0. Without it, `ICC_DIR_EL1` is trapped only by `TC` | GIC §12.4.5 p.12-349; §12.4.9 p.12-366 | **corrected** (boot-time check added) |
| 19 | `ICH_LR<n>_EL2` layout | not stated in the draft (asked in M0-A) | State [63:62], HW 61, Group 60, NMI 59, Priority [55:48], pINTID [44:32] (bit41 = EOI when HW=0), vINTID [31:0]; `S3_4_C12_C(12+n[3])_(n[2:0])`, n = 0..15 | GIC §12.4.6, pp.12-353..356 | confirmed (added as INTERFACES §7.4) |
| 20 | `ICH_VTR_EL2` | PRIbits "= 4" in the `ICC_CTLR` emulation | `ListRegs` [4:0] (LRs − 1), TDS 19, nV4 20, A3V 21, SEIS 22, IDbits [25:23] (0 = 16 bits), PREbits [28:26], PRIbits [31:29]; PRIbits is "bits minus one", minimum 0b100; `S3_4_C12_C11_1` | GIC §12.4.9, pp.12-365/366 | confirmed |
| 21 | `ICH_VMCR_EL2` | VENG1, VPMR, VBPR1, VEOIM, VCBPR | VENG0 0, VENG1 1, VAckCtl 2, VFIQEn 3, VCBPR 4, VEOIM 9, VBPR1 [20:18], VBPR0 [23:21], VPMR [31:24]; `S3_4_C12_C11_7` | GIC §12.4.8, pp.12-361..363 | confirmed |
| 22 | `ICC_SRE_EL2` | `SRE=1, Enable=1` | SRE 0, DFB 1, DIB 2, Enable 3; `Enable=0` traps EL1 accesses to `ICC_SRE_EL1`; `S3_4_C12_C9_5` | GIC §12.2.24, pp.12-283/284 | confirmed |
| 23 | System register encodings | `ICC_IAR1 S3_0_C12_C12_0`, `EOIR1 ..C12_1`, `DIR ..C11_1`, `HPPIR1`, `BPR1`, `IGRPEN1 ..C12_7`, `CTLR ..C12_4`, `PMR S3_0_C4_C6_0`, `SGI1R ..C11_5`, `ASGI1R ..C11_6`, `SGI0R ..C11_7` | all identical; additionally `HPPIR1 C12_2`, `BPR1 C12_3`, `SRE_EL1 C12_5`, `RPR C11_3`, `IAR0/EOIR0/HPPIR0/BPR0 = S3_0_C12_C8_0..3`, `IGRPEN0 C12_6` | GIC §12.2, "Accessing" tables of each register | confirmed |
| 24 | Trap of Group 1 registers (`IAR1, EOIR1, HPPIR1, BPR1, IGRPEN1, AP1Rn`) | `TALL1` | `TALL1=1` traps (EC 0x18); otherwise `IMO=1` redirects to `ICV_*` in hardware | GIC §4.6.4 p.4-67; pseudocode of each register | confirmed |
| 25 | Trap of Group 0 registers | `TALL0` | `TALL0=1` traps; otherwise `FMO=1` redirects to `ICV_*` | GIC pseudocode of IAR0/BPR0/IGRPEN0 | confirmed |
| 26 | `ICC_CTLR_EL1` | `TC` | `TC=1` traps; otherwise `IMO`/`FMO` redirect to `ICV_CTLR_EL1`. Fields: SEIS 14, IDbits 13:11, PRIbits 10:8, A3V 15, RSS 18, EOImode 1, CBPR 0 | GIC §12.2.6 p.12-233; §12.4.5 p.12-350 | confirmed |
| 27 | `ICC_PMR_EL1` | "`TC` intercepts it in variant A" | `TC=1` traps `ICC_PMR_EL1`; with `TC=0` the access goes to `ICV_PMR_EL1` (variant B keeps PMR in hardware as designed) | GIC §12.4.5 p.12-350; §12.2.19 | confirmed |
| 28 | `ICC_DIR_EL1` | `TDIR` | `TDIR=1` **or** `TC=1` | GIC §12.2 (DIR pseudocode); §12.4.5 p.12-350 | **corrected** |
| 29 | `ICC_RPR_EL1` | `TALL1`/`TC` | `TC` only (common register) | GIC §12.4.5 p.12-350 | **corrected** |
| 30 | `ICC_SRE_EL1` | `TC` | not covered by `TC`; EL1 access traps when `ICC_SRE_EL2.Enable=0` (the pseudocode has no `ICH_HCR_EL2` condition) | GIC §12.2.23 p.12-279 pseudocode; §12.2.24 p.12-283 | **corrected** |
| 31 | `ICC_SGI1R_EL1` / `SGI0R` / `ASGI1R` trap with `IMO=1` | "already intercepted with `IMO=1` [hypothesis]" | Confirmed explicitly: virtual accesses to the three registers always generate a trap taken to EL2 when `FMO=1` or `IMO=1`; the pseudocode also traps on `TC=1` | GIC §4.6.4 p.4-67; §12.2.22 p.12-277 | confirmed |
| 32 | `ICC_SGI1R_EL1` field layout | INTID 27:24, TargetList 15:0, Aff1 23:16, Aff2 39:32, Aff3 55:48, IRM 40, RS 47:44 | identical; RS is RES0 while `ICC_CTLR_EL1.RSS=0`; with IRM=1 the Aff fields and TargetList are RES0; non-existent targets are ignored by the Distributor | GIC §12.2.22 pp.12-276/277 | confirmed |
| 33 | `HCR_EL2` bit positions | RW, VM, E2H, TGE, IMO, FMO, AMO, TSC, TID3, TWI, TWE, FB | VM 0, FMO 3, IMO 4, AMO 5, FB 9, TWI 13, TWE 14, TID3 18, TSC 19, TGE 27, RW 31, E2H 34 | ARM ARM D24.2.61, pp.D24-8919..8948 | confirmed |
| 34 | `AMO=1` "needed to intercept SErrors of the SEIS errata" | rationale | the bit routes physical SErrors to EL2 when `TGE=0` (confirmed); that it is needed for the errata is project reasoning | ARM ARM D24.2.61 (AMO) | unverified (rationale only) |
| 35 | `TSC=1` makes `SMC` answerable with `NOT_SUPPORTED` | EC 0x17, "ELR advanced" | `TSC` traps EL1 SMC, EC 0x17 (AArch64); the trap's preferred return address is the SMC itself (not the next instruction), so advancing `ELR` is required | ARM ARM D24.2.61 (TSC) | confirmed |
| 36 | `TID3` traps `ID_AA64PFR0_EL1` | trap `TID3` | `TID3=1` traps EL1 reads of `ID_AA64PFR0_EL1` and the other group-3 ID registers, EC 0x18 | ARM ARM D24.2.61 (TID3) | confirmed |
| 37 | `ID_AA64PFR0_EL1.GIC` bits 27:24 = 1 | 1 | 0b0001 = GICv3.0/4.0 system-register interface; 0b0011 = GICv4.1 | ARM ARM D24.2.92, p.D24-9280 | confirmed |
| 38 | Masking candidates `ID_AA64MMFR2.NV`, SPE | named | `ID_AA64MMFR2_EL1.NV` bits [27:24]; SPE via `ID_AA64DFR0_EL1.PMSVer` [35:32] | ARM ARM D24.2.89, D24.2.79 | confirmed (which fields to mask is a design choice) |
| 39 | `VMPIDR_EL2` flat, "bit31 RES1" | `Aff0=i`, bit31 RES1 | Aff3 [39:32], bit31 RES1, U bit30, MT bit24, Aff2/1/0; **reset UNKNOWN** (must be written on each core) | ARM ARM D24.2.218, pp.D24-10099/10100 | confirmed |
| 40 | `CNTHCTL_EL2` with `E2H=1` | `EL1PCTEN=EL1PCEN=1` (format to confirm) | E2H1 layout: `EL1PCTEN` bit10, `EL1PTEN` bit11 (`EL1PCEN` exists only in the E2H0 layout, bit1). Set bits 10 and 11 | ARM ARM D24.10.2, pp.D24-11211/11212 | **corrected** |
| 41 | `CNTVOFF_EL2` basics | 0 | 64-bit, `S3_4_C14_C0_3`, virtual count = physical count − offset, **reset UNKNOWN** | ARM ARM D24.10.27, p.D24-11324 | confirmed (reset note added) |

## 4. PSCI and SMCCC

| # | Item | Draft | Spec | Source | Status |
|---|---|---|---|---|---|
| 42 | PSCI function IDs | VERSION 0x84000000, CPU_SUSPEND 0x84000001/0xC4000001, CPU_OFF 0x84000002, CPU_ON 0x84000003/0xC4000003, AFFINITY_INFO 0x84000004/0xC4000004, MIGRATE_INFO_TYPE 0x84000006, SYSTEM_OFF 0x84000008, SYSTEM_RESET 0x84000009, PSCI_FEATURES 0x8400000A | all identical (SMC32 0x84.., SMC64 0xC4..; CPU_OFF, MIGRATE_INFO_TYPE, SYSTEM_OFF/RESET and PSCI_FEATURES are SMC32 only) | PSCI §5.1, pp.24-42 | confirmed |
| 43 | PSCI return codes | SUCCESS 0 ... INVALID_ADDRESS -9 | identical (10 codes) | PSCI Table 5, p.49 | confirmed |
| 44 | `PSCI_VERSION` encoding | `0x0001_0001` = 1.1 | bits 31:16 major, 15:0 minor, so 1.1 is valid; mandatory set for 1.1 is VERSION, CPU_SUSPEND, CPU_OFF, CPU_ON, AFFINITY_INFO, SYSTEM_OFF, SYSTEM_RESET, PSCI_FEATURES; everything else optional | PSCI §5.3 Table 6; §6.9 Table 17 p.95 | confirmed |
| 45 | "PSCI 1.1, like QEMU" | like QEMU | QEMU's value is not in the Arm documents | n/a | unverified (oracle) |
| 46 | `CPU_ON` arguments and errors | x1 MPIDR, x2 entry, x3 context_id; INVALID_PARAMETERS, INVALID_ADDRESS, ALREADY_ON, ON_PENDING, INTERNAL_FAILURE | identical, plus `DENIED` (firmware policy); MPIDR format: Aff3 [39:32], Aff2 [23:16], Aff1 [15:8], Aff0 [7:0]; entry must be a physical address from the caller's view; spec silent on 4-byte alignment | PSCI §5.1.4 pp.31/32; §5.6.2 p.57 | confirmed |
| 47 | State on `CPU_ON` entry | EL1h, DAIF masked, MMU off, x0 = context_id | SP_ELNS (EL1h), `SPSR.{D,A,I,F}` = 1111, stage 1 MMU and caches off, context_id in x0, caches clean and coherent | PSCI §6.4.1, 6.4.3.3, 6.4.3.4, 6.4.3.7, pp.83-85 | confirmed |
| 48 | `AFFINITY_INFO` values | 0 ON, 1 OFF, 2 ON_PENDING | identical; args `target_affinity`, `lowest_affinity_level` (0..3, levels above 0 optional since 1.0); also INVALID_PARAMETERS, DISABLED | PSCI §5.1.5, pp.33/34 | confirmed |
| 49 | `MIGRATE_INFO_TYPE` | 2 | 2 = Trusted OS absent or no migration needed; the function is optional (NOT_SUPPORTED also valid) | PSCI §5.1.7, p.35 | confirmed |
| 50 | `PSCI_FEATURES` response | SUCCESS (0) for known, NOT_SUPPORTED (-1) otherwise | returns the feature flags (bit31 = 0; 0 when none) or NOT_SUPPORTED; the argument may also be `SMCCC_VERSION` | PSCI §5.1.14 p.42; §5.16 | **corrected** (refined) |
| 51 | `SMCCC_VERSION` | `0x8000_0000` returning `0x0001_0001` | FID 0x8000_0000; return bit31 = 0, major [30:16], minor [15:0]; mandatory from SMCCC 1.1 | SMCCC §7.2, p.29 | confirmed |
| 52 | `SMCCC_ARCH_FEATURES` | `0x8000_0001` returning NOT_SUPPORTED for everything | FID 0x8000_0001, but it must return SUCCESS when `arch_func_id` is `SMCCC_VERSION` (0x8000_0000) or `SMCCC_ARCH_FEATURES` (0x8000_0001) | SMCCC §7.3, §7.3.6, pp.30/31 | **corrected** |
| 53 | Conduit and registers | `HVC #0`, ID in w0, args x1-x3, result x0, x1-x3 zeroed | `HVC #0` required (non-zero immediates are designated for hypervisor vendors); W0 = FID, args X1-X17 (SMC64) / W1-W7 (SMC32); results X0-X17; unused result registers may be preserved or zeroed; fast call bit31, SMC64 bit30 | SMCCC §2.5, §2.6, §2.7, §2.10, pp.15-18 | confirmed |
| 54 | Unknown function | `x0 = -1` | "Unknown Function Identifier" = sign-extended -1 in R0/W0/X0 | SMCCC §5.2, p.23 | confirmed |
| 55 | Vendor hypervisor range `0xC600_0000+` | `0xC6000000` | owner 6 = Vendor Specific Hypervisor Service; SMC64 range 0xC6000000-0xC600FFFF; IDs xx_FF00-FFFF are reserved for call-count/UID/revision queries (our IDs 0..2 are outside) | SMCCC §2.5 Table 2-1; §6 Tables 6-2/6-3, pp.15, 26, 27 | confirmed |
| 56 | PSCI over HVC | HVC conduit | allowed when EL2 exists without EL3 (hypervisor providing PSCI to an EL1 OS); format identical to SMC | PSCI §5 introduction | confirmed |
| 57 | `HVC #1` returning `NOT_SUPPORTED`, Windows concludes "no Hyper-V" | hypothesis | Spec side: non-zero HVC immediates are designated for hypervisor vendors, so replying Unknown Function is allowed. What Windows does is Microsoft behavior | SMCCC §2.10 p.18 | unverified (Windows part) |

Totals: 57 items, **44 confirmed**, **10 corrected** (rows 4, 6, 9, 18, 28, 29, 30, 40, 50, 52; rows 9 and 50 are refinements), **3 unverified** (rows 34, 45, 57).

## 5. Items outside M0-A scope (left `[hypothesis]` on purpose)

These are not Arm architectural facts and therefore cannot be settled with the downloaded documents:

- INTERFACES §2 (m1n1 boot contract: registers at entry, spin-table release, QEMU `arm_load_kernel` behavior): to be verified in M3a on m1n1/QEMU sources.
- INTERFACES §4 MMIO map: t8103 virtual GICD/GICR/MSI addresses (NTASP-derived), layer region size 64 MB, AIC at most 896 IRQs.
- PPI numbering (EL2 timer 26, virtual timer 27, NS physical timer 30, maintenance 25, PMU 23): recommended by SBSA/BSA, which were not part of this task. The GIC spec only says the recommendation lives in SBSA (GIC §2.2, p.2-35).
- KVM presenting the same GICv3 model, Windows behaviors (RWP probing, `Last` search, reaction to `IDbits`), `ICC_PMR_EL1` trap frequency (RISKS R-05, to be measured from a trace).

## Notes

- **A. `GICD_TYPER.IDbits`.** The draft value `0x0178_001D` mirrors an LPI-capable GIC (16 INTID bits). Our vGIC has no LPIs, and GIC §2.2.2 limits such a GIC to 10 INTID bits, hence `0x0148_001D`. The first host test in TEST_STRATEGY was updated. Two things remain for the oracle (TEST_STRATEGY §4): what QEMU's GICv3 reports without an ITS, and whether the Windows GIC driver accepts the conformant value. If Windows rejects it, the fallback `0x0178_001D` is a deliberate, documented deviation.
- **B. Most important correction for the implementation.** The TRAP_EMULATE table in INTERFACES §7.2 had wrong trap attributions: `ICC_SRE_EL1` is controlled by `ICC_SRE_EL2.Enable` (not `TC`), `ICC_RPR_EL1` only by `TC`, and `ICC_DIR_EL1` by `TDIR` or `TC` (with `TDIR` present only if `ICH_VTR_EL2.TDS=1`). In addition `SMCCC_ARCH_FEATURES` must report `SUCCESS` for itself and for `SMCCC_VERSION`. The SGI trap with `IMO=1` is confirmed by an explicit sentence of the GIC spec, so the IPI path in INTERFACES §7.3 stands without `TC`.
- **C. Found while reading.** With `IMO=1`/`FMO=1` and no trap bit, EL1 accesses are executed by the hardware virtual CPU interface (`ICV_*`); this is the basis of variant B. On the M1 it still has to be proven that those accesses do not raise the SEIS-errata SError (RISKS R-05, M3a self-test).
