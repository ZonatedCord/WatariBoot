# Watari EL2

Minimal EL2 layer for Watari Boot (see `docs/design/watari-el2/`). This directory
currently holds the first host-testable piece: the **virtual GICv3 model**
(`vgic/`), pure state with no hardware access.

## What is in here

- `include/wel2_vgic.h`: public API (`vgic_mmio_read/write`, `vgic_set_irq_level`,
  `vgic_set_pending`, `vgic_next_pending`, `vgic_ack`, `vgic_eoi`, `vgic_route_target`).
- `vgic/vgic.c`: GICD (CTLR with DS=1, TYPER, IIDR, group/enable/pending/active,
  priority, ICFGR, IROUTER, ID registers) and per-vCPU GICR (CTLR, TYPER with the
  Last bit and affinity, WAKER sleep handshake, SGI/PPI frame). Freestanding C11
  (only `stdint.h`, `stdbool.h`, `stddef.h`).
- `tests/`: host tests (69 cases) with a tiny harness (`test.h`).

## Behavior notes

- Unimplemented offsets are RAZ/WI and reported through the `log` hook (never left
  unhandled, lesson F-14). Documented RAZ/WI registers (ITARGETSR, IGRPMODR, ...)
  are silent. Deduplication ("log-once") is the caller's job.
- Misaligned accesses or illegal sizes return `VGIC_ERR_ACCESS`, addresses outside
  the GICD/GICR windows return `VGIC_ERR_RANGE`; both are logged.
- Side effects towards the physical interrupt backend are reported through the
  `effect` hook (`UNMASK_SPI`, `MASK_SPI`, `SET_TARGET`), not performed.
- Running priority / PMR belong to the CPU interface (`cpuif`), not to this model.
- Values marked `HYPOTHESIS:` in `vgic/vgic.c` are not yet verified against the
  GICv3 specification or the QEMU oracle.

## Run the tests

```
make -C el2 test    # builds with clang -std=c11 -Wall -Wextra -Werror and runs all
make -C el2 clean
```

License: MIT (`SPDX-License-Identifier: MIT` in every file). Written from the spec;
no code from Linux KVM, m1n1 or NTASP.
