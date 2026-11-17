<!-- SPDX-License-Identifier: MIT -->
# HACR_EL2[56]: routing the PMUv3 accesses to EL2 on the M1

Status: implemented, not yet run on hardware. Everything marked [hypothesis] is settled by the first M1 run (rung HACR0, then the layer log).
Interfaces: `INTERFACES.md` 7.5. Code: `el2/boot/catch.c`, `el2/vgic/sysreg_catch.c`, `el2/boot/main.c`; probe: `tools/m3a/hacr_probe.py`.

## Problem
Windows (bootmgr first, later the kernel and EL0 code) reads PMUv3 registers: `PMCCNTR_EL0`, then `PMCR_EL0`, ... Catch mode
(`MDCR_EL2.TPM|TPMCR`) was meant to trap them and answer from a virtual PMU. On the M1 it never sees them.

## Measured facts (real M1, t8103)
- `ID_AA64DFR0_EL1 = 0x10305f09`: PMUVer = 0xF (IMPLEMENTATION DEFINED PMU), no PMUv3.
- `PMCR_EL0` read is UNDEFINED (ESR 0x02000000).
- `MDCR_EL2` write 0xe60 reads back 0xe00: `TPM` (6) and `TPMCR` (5) are RES0.
- No FEAT_FGT (no fine-grained traps as a way out).
So the PMUv3 accesses are UNDEFINED at EL1 and the guest takes the exception itself.

## Hypothesis
Bit 56 of `HACR_EL2` makes such accesses trap to EL2 instead [hypothesis, from the description of another hypervisor for this SoC; to be
measured here]. `HACR_EL2` is architectural (`S3_4_C1_C1_7`, [verified: llvm-mc]), its content is IMPLEMENTATION DEFINED, so the bit meaning is
Apple's. The access then arrives either as EC 0x18 (ISS in `ESR_EL2`, existing path) or as EC 0x3F (IMPLEMENTATION DEFINED exception class)
where the MSR/MRS ISS is expected in `AFSR1_EL2` [hypothesis: low 25 bits, same layout as the EC 0x18 ISS].

## Which ISS do we trust on EC 0x3F [hypothesis]
AFSR1_EL2 is a firmware-owned auxiliary register and may be stale, so `wel2_catch_ec3f_decode` does not trust its low 25 bits blindly:
ESR.IL must be 1 (an MSR/MRS is 32 bits); AFSR1[63:25] must be 0; AFSR1[24:0] must decode to an encoding we own; if ESR[24:0] also decodes
to an owned encoding that differs, ESR wins (AFSR1 is stale). Otherwise ESR[24:0] if it decodes, otherwise nothing. The first 8 raw
`ec3f: ESR= AFSR1= ELR=` lines show which source won (`iss=afsr1|esr|none`), so the hardware run settles it.
An EC 0x3F from a lower EL that is not an owned catch is **not fatal**: it is reflected to the guest as an EL1 UNDEF (`wel2_inject_undef`,
what the guest got before HACR) and logged (same 8 lines). `fatal()` remains for EC 0x3F from EL2 itself, or when injection is impossible
(AArch32 guest).

## How we verify it
1. Before the layer: rung `HACR0` (`hacr_probe.py`): does bit 56 stick on the boot core (readback), is the old value restored.
2. Optional, by hand: `hacr_probe.py --el1`: an EL1 `mrs PMCCNTR_EL0` through the proxy's el1_call; reports EC 0x18 / 0x3F with `AFSR1_EL2`, or no arrival.
3. In the layer: the log line `hacr: before= after= requested=` per core, `ec3f: ESR= AFSR1= ELR= iss=afsr1|esr|none` for the first 8 EC 0x3F
   exceptions, `CATCH ...` lines per distinct encoding, and the fixed status row on the panel (`MDCR STUCK=.. HACR=.. CATCH N=.. LAST=..`).

## Data flow
```
boot args wel2.hacr=<mask> (M1 default 1<<56)  -->  g_hacr
every core, before the guest:  catch_cpu_init -> HACR_EL2 |= g_hacr (read back, logged); MDCR_EL2 |= trap bits (best effort)
guest PMUv3 access --(HACR[56])--> EL2: EC 0x18 -> catch_trap(ESR ISS)
                                        EC 0x3F -> catch_trap_ec3f(AFSR1 ISS, else ESR ISS)
          -> catch_handle: eff mask = g_catch_mask | (g_hacr ? PMU : 0) -> record, log, virtual PMU (wel2.vpmu), ELR += 4
          -> status row (new encoding at once, else at most 2 Hz)
```
The PMU group is "enabled" whenever HACR routes it, decoupled from what stuck in MDCR.

## Risks
- Bit 56 may trap more than the PMU (other IMPLEMENTATION DEFINED registers). Mitigation: everything caught is logged with its encoding; an EC 0x3F
  or EC 0x18 that is not an owned encoding stays on the fatal path, which prints ESR, AFSR1 and ELR (and AFSR1 for every fatal now).
- EL0 accesses: PMUSERENR_EL0 does not exist as hardware here; whether EL0 accesses are routed the same way is unknown. The virtual PMUSERENR logic stays as is; the status row and the log show what arrives.
- Per core: HACR_EL2 is per core. It is programmed on the boot core and in the secondary start path (`catch_cpu_init(false)`), and read back per core; a secondary whose readback lacks the requested bits logs a line, and the final statistics print every core's value.
- If bit 56 does nothing (reads back 0, or the guest still takes the UNDEF): the status row shows `HACR=0x0` or `N=0` while Windows dies at the same place. Fallbacks, not implemented: a different bit mask via `wel2.hacr=<mask>` for experiments; a shim in the guest firmware (EDK2) that avoids the PMU accesses; reconsider emulation by decoding the UNDEF at EL1 (not possible without owning the guest vectors).
- QEMU has no such register: `wel2.hacr` is ignored there, with a warning, and HACR is never touched.
- A wrong write to an IMPLEMENTATION DEFINED register could misbehave: the default is the single documented bit, ORed into the firmware's value, never a full write.
