# INTERFACES: contracts between components

- Project: Watari Boot · Date: 2026-10-12 · Version: 0.1 · Status: draft
- Related documents: [PRD](PRD.md) · [ARCHITECTURE](ARCHITECTURE.md) · [TEST_STRATEGY](TEST_STRATEGY.md) · [RISKS](RISKS.md)

## Legend
- `[verified: source]`: confirmed against the cited source.
- `[hypothesis]`: not verified. Sections marked **HYPOTHESIS (depends on Phase 0)** must not be implemented definitively until the Phase 0 spike is concluded. Code written before that must be isolated behind a replaceable interface.
- `[proposal]`: our own design choice (not an external fact); modifiable via ADR.

---
## 1. Handoff m1n1 -> EDK2  [proposal; format does not exist upstream, to be defined]
Status: no verified source on an existing m1n1 -> EDK2 handoff mechanism. The contract below is a proposal. [hypothesis] m1n1 can load a payload and pass a pointer to the ADT (typical m1n1 behavior with Linux payloads/Apple boot args; to be verified in m1n1 `src/`).

| Element | Proposed contract |
|---|---|
| Entry | EDK2 received in RAM at a fixed physical address, execution at EL2 or EL1 (to be decided: [hypothesis] the M1 starts at EL2; verify), MMU off or identity-mapped, interrupts masked (IRQ and FIQ) |
| Register x0 | physical pointer to `PI_HANDOFF` (structure below) |
| ADT | ADT blob in memory, unchanged from the one from iBoot, not relocated after the handoff |
| Framebuffer | physical base, width, height, stride, pixel format, size |
| Memory map | list of regions (base, size, type: usable/firmware-reserved/MMIO) computed by m1n1 |
| UART/debug | debug UART MMIO base + speed |
| What m1n1 must have done | PMGR of the required blocks, clocks, framebuffer, USB PHY/DWC3 in host mode (from the plan; [hypothesis] the feasibility of each point depends on m1n1 support for t8103) |
| What EDK2 must no longer ask of m1n1 | no call back after the handoff |

```c
/* proposal, version 1 */
typedef struct {
  UINT32 Magic;        /* 'PIHF' */
  UINT32 Version;      /* 1 */
  UINT64 AdtBase;  UINT64 AdtSize;
  UINT64 FbBase;   UINT32 FbWidth, FbHeight, FbStride, FbPixelFormat;
  UINT64 MemMapBase; UINT32 MemMapCount, MemMapEntrySize;
  UINT64 UartBase; UINT32 UartBaud;
  UINT64 Reserved[8];
} PI_HANDOFF;
```
Errors: wrong Magic/Version -> EDK2 halts with a message on framebuffer/UART. Compatibility: fields are added only in `Reserved`; a change of meaning = increment of Version.

## 2. ADT data model -> tables  [proposal]
`M-ADT-PARSE` exposes a neutral model: CPU list (id, cluster, P/E class, MPIDR [hypothesis: provided by the ADT]), MMIO blocks, interrupts (AIC number), clocks/PMGR. M-ACPI-GEN consumes only this model. Contract: no data is read twice from the ADT in different modules.

## 3. ACPI tables

General rule: the binary layouts of the standard ACPI tables are defined by the ACPI specification; here we only declare how we populate them. [hypothesis] The exact fields must be checked against the current ACPI specification before implementation (Phase 1).

### 3.1 MADT  **HYPOTHESIS (depends on Phase 0)**
- [hypothesis] A "GIC-less" MADT (no GICD structure) is accepted by Windows if an HAL extension provides the controller. This is the central point and it is **not verified**.
- [hypothesis] Windows might require GICC structures to enumerate processors (the GICC structure contains ACPI Processor UID, flags, parking protocol version and parked address). [verified: Linux `acpi_parking_protocol.c`] the Linux kernel obtains the parking address and `parking_version` from the MADT GICC structure. So for the parking protocol a GICC is used *without* exposing a working GIC: coexistence to be validated in Phase 0/1 (RISKS R-02).
- Alternative if Windows insists on GICD: describe a dummy GIC (inaccessible device) and intercept it with the HAL extension - **internal plan B hypothesis**, not in the original plan.
- One GICC per core (8), with sequential ACPI Processor UID, `Parking Protocol Version` != 0 and `Parked Address` = the core's mailbox [verified: Linux, checks `mailbox_addr && version`].

### 3.2 GTDT  **HYPOTHESIS (depends on Phase 0)**
- [hypothesis] Windows ARM64 expects the architectural timer described in the GTDT with an interrupt (typically a GIC PPI GSIV). On the M1 the timer arrives as FIQ [verified: irq-apple-aic.c, FIQ for ARM timer]. Plan strategy: timer source on IRQ provided by an HAL timer extension [hypothesis: mechanism to be confirmed in Phase 0; [hypothesis] on the hardware possibility of routing the timer to IRQ - to be verified with Asahi/m1n1 documentation].
- Fields to be defined after Phase 0: GSIV for secure/non-secure/virtual/hyp timers, polarity/trigger flags, possible use of Platform Timer / Memory-mapped timer.

### 3.3 CSRT  **HYPOTHESIS (depends on Phase 0)**
- [verified: Linux `drivers/acpi/csrt.c`; Microsoft HLK] CSRT is an ACPI table with resources for devices not in the DSDT (e.g. DMA); Windows associates an HAL extension with an ACPI PNP ID.
- [hypothesis] Layout: resource groups with Vendor ID, Device ID, Subsystem ID, Revision, Resource Descriptors (type, subtype, UID); the AIC HAL extension and the timer one require one group each with dedicated IDs. ID values: **to be decided** (reserved Microsoft IDs are not used; to be defined with our own vendor ID).
- [hypothesis] The DLL file name and the CSRT -> DLL association (INF/registry/`HalExtension`) are Phase 0 details.

### 3.4 PPTT
- [hypothesis, from the plan] P/E cores described with processor/cluster/cache hierarchy and efficiency classes (Efficiency Class field in the ACPI PPTT processor hierarchy node, from ACPI spec >= 6.x). Contract: 1 P cluster node (4 cores), 1 E cluster node (4 cores), L1/L2 cache per cluster; values read from the ADT. [hypothesis] Cache sizes from the ADT are available; to be verified.
- Verification: compare with what Windows reports (Task Manager/`Get-CimInstance`) in Phase 4.

### 3.5 DSDT/SSDT
Minimal devices (IDs to be assigned, not invented here):
| Node | Purpose | Phase |
|---|---|---|
| AIC device (our HID) | Hook for the HAL extension | 3 |
| IRQ timer device | Hook for the timer extension | 3 |
| xHCI (compatible with `PNP0D10`? [hypothesis]) | USB controller for UsbXhci | 4 |
| ANS | Hook for the StorPort miniport | 4 |
| Processors with `_CPC` | DVFS | 5 |
| SPI/HID, battery, PEP | peripherals | 5 |
Contract: every device has `_HID`/`_UID`, `_CRS` with MMIO and AIC interrupt, `_STA`. Interrupts in `_CRS` use logical GSIVs assigned by the HAL extension [hypothesis: GSIV model to be verified in Phase 0].

### 3.6 Other tables
FADT (HW-reduced flag [hypothesis: needed, there is no legacy ACPI hardware]), DBG2 for serial debug [hypothesis: used by KDCOM], SPCR, XSDT/RSDP. All to be verified against the specifications in Phase 1.

## 4. Parking protocol mailbox
- [verified: https://codebrowser.dev/linux/linux/arch/arm64/kernel/acpi_parking_protocol.c.html] Per-core structure:
```c
struct parking_protocol_mailbox {  /* little-endian */
  UINT32 cpu_id;      /* firmware: 0xFFFFFFFF while parked */
  UINT32 reserved;
  UINT64 entry_point; /* physical entry address of the OS */
};
```
- [verified: same source] OS sequence: writes entry_point, then cpu_id with the CPU's GIC interface ID, then sends a wake-up IPI. Linux note: device nGnRE mapping instead of nGnRnE declared "harmless".
- [verified: same source, detail obtained from a summary, to be re-checked in the specification] Each CPU has a 4K page with spin code and mailbox (reference to U-Boot patches).
- [hypothesis] The official specification ("Multi-processor Startup for ARM Platforms") was not read: barrier ordering, memory attributes and the exact behavior of the "wake" must be checked before implementation.
- **Open design point [hypothesis]**: the protocol uses a wake-up IPI (in the GIC world an SGI). Here there is no GIC; the parked core should wait on `WFE`/mailbox polling or on an AIC IPI (two IPIs per CPU [verified: irq-apple-aic.c]). The EDK2 parking code polls `cpu_id` with `WFE` and sending an AIC event/IPI is an optional wake-up. If Windows sends the wakeup through the interface of the controller registered by the HAL extension, the extension must implement `sendIPI`. **To be verified in Phase 0** (how Windows sends the boot IPI).
- Expected `cpu_id` = identifier in the GICC (CPU Interface Number field); in the absence of a GIC we will use an index 0..7 [proposal].
- Errors: if entry_point remains invalid, the core stays parked; EDK2 receives no notification; the Phase 2 test must verify that all 8 are in the "parked" state before the handoff to bootmgr.

## 5. HAL extension interface  **HYPOTHESIS (depends on Phase 0)**
Nothing here is verified. The following functions and structures are conceptual placeholders that Phase 0 must confirm, rename or discard.

Verified facts: [verified: Microsoft HLK, link in ARCHITECTURE sec. 1] HAL Extension DLLs exist, hooked to an ACPI PNP ID and loaded/evaluated based on CSRT.

Working hypotheses (from the plan):
| Element | Hypothesis | Status |
|---|---|---|
| Entry point | Exported initialization function of the extension (`HalExtension`-like) | not verified |
| Controller registration | Call equivalent to `HalpInterruptRegisterController` with a function table (`_INTERRUPT_FUNCTION_TABLE`) and a `KnownType` for non-GIC controllers | not verified: existence and accessibility from third-party extensions unknown |
| Controller functions | enable/disable line, EOI, accept interrupt, set affinity, send IPI, query/mapping GSIV | not verified |
| Timer | timer source registration from the HAL ext ("documented" mechanism according to the plan) | not verified |
| Imports | the extension may import only from `hal.dll`/`ntoskrnl.exe` according to the HLK "no imports" test [hypothesis, HLK page incomplete] | not verified |
| Signing | test-signing in development; Microsoft signing for distribution | plan |

Process obligation: every function in this table becomes `[verified]` only with a reference to a report in `re/` (address, function, pseudocode). Until then the `halext-aic` code implements an internal interface `IIrqController` (registration, mask, unmask, eoi, setAffinity, sendIpi, mapGsiv) that isolates the back-end and allows the external ABI to change without touching the AIC logic [proposal].

### 5.1 Internal AIC interface [proposal, hardware part verified]
[verified: irq-apple-aic.c] 896 level-triggered IRQs, per-IRQ mask/affinity, 2 IPIs per CPU, event/IPI-send/IPI-ack/IPI-mask registers (exact offsets in the Linux source; not reproduced here). The port starts from the Linux source with a provenance header (license to be checked).
Precondition: FIQs masked (PSTATE.F=1) throughout the Windows path [hypothesis: Windows does not unmask them]. Postcondition: no use of Fast IPI, no dependence on FIQ for the timer (ADR-0004).

## 6. Drivers
- ANS StorPort: standard StorPort entry points (HwFindAdapter, HwInitialize, HwStartIo, HwInterrupt) [verified: standard structure of StorPort miniports in the WDK, to be reconfirmed]. NVMe part reusable, ANS/SART init to be ported [hypothesis, from the plan].
- DART/SART: internal library used by ANS/SPI-HID. DART 16K pages vs Windows 4K [hypothesis, from the plan]; contract: `DartMap(dev, va, pa, len)` with len a multiple of 16K, or bypass.
- PEP: Platform Extension Plugin callbacks [hypothesis: interface from the WDK]. PMGR registers for DVFS [hypothesis].
- SPI HID: KMDF HID minidriver; MTP/SPI protocol from the Asahi driver [hypothesis].

## 7. Conventions
Names: prefix `Pi` for platform types, `Aic` for AIC; errors as `EFI_STATUS` codes (firmware) and `NTSTATUS` (Windows); every module documents preconditions/postconditions in comments.
