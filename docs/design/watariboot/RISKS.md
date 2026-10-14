# RISKS: risk register

- Project: Watari Boot · Date: 2026-10-12 · Version: 0.1 · Status: draft
- Related documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [INTERFACES](INTERFACES.md) · [TEST_STRATEGY](TEST_STRATEGY.md)
- Probability and impact are the designer's qualitative estimates (H/M/L), not measured: `[hypothesis]`.

| ID | Risk | Prob. | Impact | Mitigation | Plan B |
|---|---|---|---|---|---|
| R-01 | Negative spike: the HAL does not accept a non-GIC controller from third-party extensions [hypothesis, central hypothesis] | M | Critical | Phase 0 before anything else; evidence in `re/`; isolate the interface (INTERFACES 5) | (a) EL2 shim with the virtualization GIC interface (`ICH_*`) for interrupts only, estimated overhead ~1% [plan hypothesis, not measured]; (b) kernel patch for research, not distributable. The user decides (ADR-0001) |
| R-02 | Windows requires a GIC (GICD/GICR) in the MADT to enumerate CPUs or for parking [hypothesis] | M | High | Verify in Phase 0/1 on ntoskrnl and in QEMU | Dummy GIC in ACPI intercepted by the extension; then the R-01 plan B |
| R-03 | FIQ-only timer (the ARM timer goes to FIQ [verified: irq-apple-aic.c]) and cannot be routed to IRQ | M | High | Study the M1 registers/modes in m1n1/Asahi; alternative timer source on IRQ | Timer polling via the extension, or an EL2 shim; decide with the user |
| R-04 | Debugging without a second computer, and the Windows debug channel is unavailable on non-standard hardware (KDCOM requires a described UART; KDNET requires a supported NIC) [hypothesis] | H | High | Pi 5 as debug host from Phase 2; DBG2 + debug UART; framebuffer log; m1n1 as proxy | Debug via m1n1 (memory) without WinDbg; bugcheck on framebuffer |
| R-05 | Brick/data loss on the Mac | L | Critical | Separate Asahi partition only; backup; confirmation before every operation; macOS and recovery left intact | Restore via DFU from another Mac (requires a second Mac that the user does not have [plan: note]); access to one must be arranged before Phase 2 |
| R-06 | Parking protocol: wake via IPI does not work without a GIC; exact semantics not verified against the specification | M | High | Read the specification; polling with WFE in the parking loop; send AIC IPI | Single core (degraded) for phases 3-4; then resolve |
| R-07 | Non-standard Apple CPU behavior that Windows does not tolerate (IMPDEF registers, SError, page size/granularity, ID registers) [hypothesis] | M | High | Analysis in Phase 0; test in Phase 3 with bugcheck logs | If not fixable via firmware: EL2 shim |
| R-08 | DART IOMMU (16K pages) incompatible with Windows DMA (4K) | M | Medium-High | Bypass where possible; mappings in the drivers; SART for ANS | 16K bounce buffers in our drivers |
| R-09 | ANS2 SSD: complex init and SART to port; risk of data corruption | M | High | Port from the Asahi driver; test on a dedicated partition; host model | Boot and install on external USB (external SSD) |
| R-10 | m1n1/EDK2 firmware: USB/PHY/framebuffer instability | M | Medium | Reuse m1n1 code; repeated tests | Boot from USB with the controller already initialized by m1n1 |
| R-11 | Windows updates and changes behavior (build different from Phase 0) | M | Medium | Freeze build and hash; regression after every update | Stay on the frozen build |
| R-12 | Driver signing: test-signing during development; distribution requires Partner Center [from the plan] | H (for distribution) | Medium | Distinguish research goal / distribution | Distribution only as instructions/test mode |
| R-13 | Legal: use of Microsoft data and reverse engineering [from the plan: permitted for interoperability in the EU, Dir. 2009/24/EC art. 6; not verified here, not legal advice] | L | High | No Microsoft binaries/code in the repo (ADR-0005); notes in our own words | Legal opinion if publishing |
| R-14 | Asahi code licenses (MIT/GPL-2.0) incompatible with the license chosen for the repo | M | Medium | Provenance headers; choose the license after an audit | Clean-room rewrite of the GPL modules |
| R-15 | Complexity for a user without firmware experience; progress limited by execution time on the hardware | H | Medium | Agents write, the user runs; step-by-step procedures | Reduce scope to Phase 3 as a research goal |
| R-16 | Dependence on upstream Asahi changes (m1n1, ADT) | M | Medium | Pin commits; provenance headers | Maintained fork |
| R-17 | Phase 5: WDDM GPU is very large and out of reach | H | Medium | Basic Display on GOP; WDDM as a separate project | No acceleration |

## Notes
Priorities for the immediate session: R-01, R-02, R-03 can be resolved (at least in part) with Phase 0 alone and require no hardware.
