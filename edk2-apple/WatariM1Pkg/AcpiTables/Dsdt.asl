/*
 * DSDT v0 of the Apple M1 platform (j293) under Watari EL2: the eight processor devices and
 * nothing else (docs/design/watari-edk2-m1/DESIGN.md 9.2). _UID n equals the MADT's GICC
 * processor UID n; tools/check-dsdt-uids.py fails the build if the two lists differ.
 * Deliberately absent, each to be added with the milestone that needs it: PCI host bridge +
 * MCFG, xHCI, power button, _S5, debug-port device, thermal zones, battery.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
DefinitionBlock ("Dsdt.aml", "DSDT", 2, "WATARI", "J293    ", 1)
{
  Scope (\_SB)
  {
    Device (CPU0) { Name (_HID, "ACPI0007") Name (_UID, 0) }   // E core, MADT UID 0
    Device (CPU1) { Name (_HID, "ACPI0007") Name (_UID, 1) }   // E core
    Device (CPU2) { Name (_HID, "ACPI0007") Name (_UID, 2) }   // E core
    Device (CPU3) { Name (_HID, "ACPI0007") Name (_UID, 3) }   // E core
    Device (CPU4) { Name (_HID, "ACPI0007") Name (_UID, 4) }   // P core
    Device (CPU5) { Name (_HID, "ACPI0007") Name (_UID, 5) }   // P core
    Device (CPU6) { Name (_HID, "ACPI0007") Name (_UID, 6) }   // P core
    Device (CPU7) { Name (_HID, "ACPI0007") Name (_UID, 7) }   // P core, MADT UID 7
  }
}
