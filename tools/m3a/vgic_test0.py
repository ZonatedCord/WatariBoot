# SPDX-License-Identifier: MIT
# Does the M1's GICv3 virtual CPU interface deliver a list-register interrupt to EL1?
# On core 0 (el1_call): EL2 writes ICH_VMCR/ICH_LR0/ICH_HCR (volatile), EL1 reads ICC_IAR1 and writes ICC_EOIR1,
# then EL2 reads the LR back and restores everything. Writes CPU registers of one core only.
import sys
from m1n1.proxy import UartInterface, M1N1Proxy, REGION_RX_EL1
from m1n1.proxyutils import ProxyUtils, bootstrap_port
log = open(sys.argv[1], "w")
def say(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); log.write(s + "\n"); log.flush()
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
CPU = 0
el2 = None
el1 = "el1"
ICC_SRE_EL2, ICH_HCR, ICH_VMCR, ICH_LR0, ICH_ELRSR, ICH_MISR = (3,4,12,9,5), (3,4,12,11,0), (3,4,12,11,7), (3,4,12,12,0), (3,4,12,11,5), (3,4,12,11,2)
ICC_IAR1, ICC_EOIR1, ICC_SRE_EL1, ICC_PMR = (3,0,12,12,0), (3,0,12,12,1), (3,0,12,12,5), (3,0,4,6,0)
HCR_EL2 = (3,4,1,1,0)
rd = lambda r, c=el2: u.mrs(r, call=c)
say(f"cpu{CPU} HCR_EL2=0x{rd(HCR_EL2):x}")
o_sre, o_hcr, o_vmcr, o_lr0 = rd(ICC_SRE_EL2), rd(ICH_HCR), rd(ICH_VMCR), rd(ICH_LR0)
say(f"before: ICC_SRE_EL2=0x{o_sre:x} ICH_HCR=0x{o_hcr:x} ICH_VMCR=0x{o_vmcr:x} ICH_LR0=0x{o_lr0:x} ELRSR=0x{rd(ICH_ELRSR):x}")
VINTID = 42
lr = (1 << 62) | (1 << 60) | (0xa0 << 48) | VINTID      # pending, group 1, prio 0xa0, vINTID 42
try:
    if not o_sre & 0x8:
        u.msr(ICC_SRE_EL2, o_sre | 0x9, call=el2)      # SRE + Enable (EL1 may use sysregs)
    u.msr(ICH_VMCR, 0xff000002, call=el2)              # VPMR 0xff, VENG1
    u.msr(ICH_LR0, lr, call=el2)
    u.msr(ICH_HCR, 1, call=el2)                         # En
    say(f"armed: ICH_LR0=0x{rd(ICH_LR0):x} ICH_HCR=0x{rd(ICH_HCR):x} MISR=0x{rd(ICH_MISR):x}")
    try:
        say(f"EL1 view: ICC_SRE_EL1=0x{rd(ICC_SRE_EL1, el1):x} ICC_PMR=0x{rd(ICC_PMR, el1):x}")
    except Exception as e:
        say(f"EL1 SRE/PMR read: EXCEPTION {e}")
    iar = rd(ICC_IAR1, el1)
    say(f"EL1 ICC_IAR1_EL1 = {iar}  ({'DELIVERED' if iar == VINTID else 'not delivered'})")
    say(f"after ack: ICH_LR0=0x{rd(ICH_LR0):x} (state {(rd(ICH_LR0) >> 62) & 3}: 2 = active)")
    if iar == VINTID:
        u.msr(ICC_EOIR1, iar, call=el1)
        lr2 = rd(ICH_LR0)
        say(f"after EOI: ICH_LR0=0x{lr2:x} (state {(lr2 >> 62) & 3}: 0 = done) ELRSR=0x{rd(ICH_ELRSR):x}")
    say(f"second IAR (expect 1023): {rd(ICC_IAR1, el1)}")
except Exception as e:
    say(f"EXCEPTION {type(e).__name__}: {e}")
finally:
    for r, v in ((ICH_HCR, o_hcr), (ICH_LR0, o_lr0), (ICH_VMCR, o_vmcr), (ICC_SRE_EL2, o_sre)):
        try: u.msr(r, v, call=el2)
        except Exception as e: say(f"restore failed {r}: {e}")
    say(f"restored: ICH_HCR=0x{rd(ICH_HCR):x} ICH_LR0=0x{rd(ICH_LR0):x} ICC_SRE_EL2=0x{rd(ICC_SRE_EL2):x}")
