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
- `include/wel2_intr_map.h`, `vgic/intr_map.c` (M2.x-3): shared interrupt map hwirq <-> vINTID/GSIV (32 + hwirq, MSI pool 704-735 -> 928-959, default-deny allow-list, trigger per entry).
- `include/wel2_pirq.h`, `vgic/pirq.c` (M2.x-2): physical source -> vINTID policy (level/edge, re-target on CPU_OFF/CPU_ON) behind `struct pirq_ops`.
- `include/wel2_pirq_guard.h`, `vgic/pirq_guard.c` (#67, host-only so far): storm limiter decorator over `pirq_ops` (rate budget per window with the caller's timer count, mask + cool-down, log once).
- `include/wel2_v2m.h`, `vgic/v2m.c` (#67, host-only): GICv2m-style MSI frame model (TYPER, SETSPI_NS doorbell -> pending edge SPI, IIDR) for the M3a frame design; not in the boot path.
- `include/wel2_ram.h`, `vgic/ram_map.c` (#60): guest RAM range from the FDT `/memory` node (2 MB rounding, clamp, which 1 GB entries are full/partial/not RAM), used by the EL2 stage-1 and stage-2 builders.
- `tests/`: host tests with a tiny harness (`test.h`); `fake_pirq.h` is the fake GIC/AIC backend.

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
