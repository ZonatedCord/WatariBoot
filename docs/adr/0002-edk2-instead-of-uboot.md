# ADR-0002: EDK2 instead of U-Boot

- Status: Accepted (from the plan)
- Date: 2026-10-12 · Links: [ARCHITECTURE](../design/watariboot/ARCHITECTURE.md), [INTERFACES](../design/watariboot/INTERFACES.md)

## Context
Windows ARM64 requires UEFI and ACPI [hypothesis: requirement from the plan, not rechecked against Microsoft documentation]. GOP, UEFI runtime services, ACPI, the parking protocol and CSRT are needed. The plan names `ArmVirtPkg` and `Platform/RaspberryPi` from edk2-platforms as models.

## Decision
Use an EDK2 platform (`AppleSiliconPkg`) started by m1n1, with ACPI generated from the ADT.

## Alternatives (rejected)
- U-Boot with UEFI: UEFI support and the parking protocol exist in U-Boot patches [verified: U-Boot mailing list denx.de, "arm: cpu: Add ACPI parking protocol support"], but U-Boot's UEFI/ACPI support for booting Windows was not evaluated here [hypothesis]. Rejected in the plan; the detailed rationale is not documented and must be completed if the choice is reopened.
- Custom bootloader: rejected, high maintenance cost.
- Use m1n1 directly as UEFI: does not exist.

## Consequences
- We inherit the EDK2 ecosystem (build, USB/GOP/ACPI drivers) and its build complexity.
- There is an existing model for Windows on non-standard hardware (Raspberry Pi) [verified: cited in the plan, not confirmed here].
- The m1n1 -> EDK2 contract must be defined by us (INTERFACES 1).
