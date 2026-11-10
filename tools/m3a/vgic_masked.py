# SPDX-License-Identifier: MIT
# vGIC delivery test on core 0 with IRQs masked at EL2 first, so EL1 is entered masked and no
# exception is taken: EL1 reads ISR_EL1 and ICC_IAR1_EL1, EOIs, reads IAR again. Restores everything.
import sys
from m1n1.proxy import UartInterface, M1N1Proxy, GUARD
from m1n1.proxyutils import ProxyUtils, bootstrap_port
log = open(sys.argv[1], "w")
def say(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); log.write(s + "\n"); log.flush()
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
DAIF = (3, 3, 4, 2, 1)
ICC_SRE_EL2, ICH_HCR, ICH_VMCR, ICH_LR0, ICH_ELRSR = (3,4,12,9,5), (3,4,12,11,0), (3,4,12,11,7), (3,4,12,12,0), (3,4,12,11,5)
ICC_IAR1, ICC_EOIR1, ISR_EL1 = (3,0,12,12,0), (3,0,12,12,1), (3,0,12,1,0)
DAIFSET3, DAIFCLR3 = 0xd50343df, 0xd50343ff
p.set_exc_guard(GUARD.SKIP)                 # a fault skips the instruction instead of rebooting
u.exec((DAIFSET3,))
d = u.mrs(DAIF)
say(f"core0 EL2 DAIF after daifset = 0x{d:x}")
if (d & 0xc0) != 0xc0:
    say("ABORT: could not mask IRQ/FIQ at EL2"); sys.exit(1)
o_hcr, o_vmcr, o_lr0 = u.mrs(ICH_HCR), u.mrs(ICH_VMCR), u.mrs(ICH_LR0)
VINTID = 42
try:
    say(f"EL1 ISR_EL1 before arming = 0x{u.mrs(ISR_EL1, call='el1'):x}")
    u.msr(ICH_VMCR, 0xff000002)
    u.msr(ICH_LR0, (1 << 62) | (1 << 60) | (0xa0 << 48) | VINTID)
    u.msr(ICH_HCR, 1)
    say(f"armed: LR0=0x{u.mrs(ICH_LR0):x} HCR=0x{u.mrs(ICH_HCR):x}")
    isr = u.mrs(ISR_EL1, call='el1')
    say(f"EL1 ISR_EL1 = 0x{isr:x}  (bit 7 I = virtual IRQ pending: {bool(isr & 0x80)})")
    iar = u.mrs(ICC_IAR1, call='el1')
    say(f"EL1 ICC_IAR1_EL1 = {iar}  -> {'DELIVERED' if iar == VINTID else 'NOT delivered'}")
    lr = u.mrs(ICH_LR0)
    say(f"EL2 ICH_LR0 after IAR = 0x{lr:x} (state {(lr >> 62) & 3}: 2 = active)")
    if iar == VINTID:
        u.msr(ICC_EOIR1, iar, call='el1')
        lr = u.mrs(ICH_LR0)
        say(f"after EOI: ICH_LR0 = 0x{lr:x} (state {(lr >> 62) & 3}: 0 = done), ELRSR = 0x{u.mrs(ICH_ELRSR):x}")
    say(f"EL1 IAR again = {u.mrs(ICC_IAR1, call='el1')} (1023 = none)")
    say(f"exceptions counted by m1n1: {p.get_exc_count()}")
except Exception as e:
    say(f"EXCEPTION {type(e).__name__}: {e}")
finally:
    for r, v in ((ICH_HCR, o_hcr), (ICH_LR0, o_lr0), (ICH_VMCR, o_vmcr)):
        try: u.msr(r, v)
        except Exception as e: say(f"restore failed {r}: {e}")
    u.exec((DAIFCLR3,))
    p.set_exc_guard(GUARD.OFF)
    say(f"restored: HCR=0x{u.mrs(ICH_HCR):x} LR0=0x{u.mrs(ICH_LR0):x} DAIF=0x{u.mrs(DAIF):x}")
