/*
 * Minimal DSDT for the QEMU "virt" bench machine (E13b/E13c, docs/design/watari-edk2-m1/DESIGN.md 9.2).
 * Written from the ACPI 6.x specification and QEMU virt hardware facts (addresses from its device tree);
 * not derived from QEMU's generated tables. Contents: four processor devices and one PCIe host bridge.
 *
 * Hardware facts used [verified: device tree dumped with
 *   -M virt,virtualization=on,gic-version=3,its=off,msi=gicv2m -smp 4 ... ,dumpdtb=]:
 *   - ECAM (pci-host-ecam-generic) at 0x40_1000_0000, size 0x1000_0000 (buses 0-255)
 *   - PCI IO window: CPU address 0x3EFF_0000, size 0x1_0000, PCI address 0
 *   - 32-bit MMIO window: 0x1000_0000, size 0x2EFF_0000 (identity mapped)
 *   - 64-bit MMIO window: 0x80_0000_0000, size 0x80_0000_0000 (identity mapped)
 *   - INTx: SPIs 3..6 (GSI = 32 + SPI = 35..38); slot s, pin p (0 = INTA) uses SPI 3 + ((s + p) mod 4),
 *     level triggered (interrupt-map of the device tree)
 *   - the bus is DMA coherent (dma-coherent property, so _CCA = 1)
 *   - four processors (-smp 4 of el2/boot/run-m2.sh); _UID = MADT GICC UID = CPU index
 * The MCFG table (ECAM base) is provided by the firmware outside the DSDT.
 *
 * SPDX-License-Identifier: MIT
 */
DefinitionBlock ("qemu-virt-min.aml", "DSDT", 2, "WATARI", "QVIRTMIN", 1)
{
  Scope (\_SB)
  {
    // Processor devices: ACPI "Processor Device" ACPI0007 (ACPI 6.x sec. 8.4), _HID sec. 6.1.5, _UID sec. 6.1.12.
    Device (CPU0) { Name (_HID, "ACPI0007") Name (_UID, 0) }
    Device (CPU1) { Name (_HID, "ACPI0007") Name (_UID, 1) }
    Device (CPU2) { Name (_HID, "ACPI0007") Name (_UID, 2) }
    Device (CPU3) { Name (_HID, "ACPI0007") Name (_UID, 3) }

    // PCI Express root bridge: _HID PNP0A08 with _CID PNP0A03 (PCI Firmware spec 3.x sec. 4.1; ACPI sec. 6.1.2).
    Device (PCI0)
    {
      Name (_HID, EisaId ("PNP0A08"))
      Name (_CID, EisaId ("PNP0A03"))
      Name (_SEG, 0)    // ACPI sec. 6.5.6: PCI segment group 0 (the single MCFG entry)
      Name (_BBN, 0)    // ACPI sec. 6.5.5: base bus number
      Name (_UID, 0)    // ACPI sec. 6.1.12
      Name (_CCA, 1)    // ACPI sec. 6.2.17: DMA is cache coherent (device tree: dma-coherent)

      // _CRS (ACPI sec. 6.2.2; descriptors sec. 6.4.3.5): the windows the bridge produces.
      Name (_CRS, ResourceTemplate ()
      {
        // Bus range 0-255 (sec. 6.4.3.5.5, Word Address Space, bus-number type).
        WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
          0x0000,  // granularity
          0x0000,  // minimum
          0x00FF,  // maximum
          0x0000,  // translation
          0x0100)  // length
        // 32-bit MMIO window, identity mapped (sec. 6.4.3.5.2, DWord Address Space).
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
          0x00000000,   // granularity
          0x10000000,   // minimum
          0x3EFEFFFF,   // maximum = minimum + length - 1
          0x00000000,   // translation
          0x2EFF0000)   // length
        // 64-bit MMIO window, identity mapped (sec. 6.4.3.5.1, QWord Address Space).
        QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
          0x0000000000000000,   // granularity
          0x0000008000000000,   // minimum
          0x000000FFFFFFFFFF,   // maximum
          0x0000000000000000,   // translation
          0x0000008000000000)   // length
        // IO window: PCI ports 0..0xFFFF appear at CPU address 0x3EFF0000 (TypeTranslation: the IO range is
        // memory mapped on the primary side; translation = CPU address - PCI address).
        QWordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
          0x0000000000000000,   // granularity
          0x0000000000000000,   // minimum (PCI side)
          0x000000000000FFFF,   // maximum (PCI side)
          0x000000003EFF0000,   // translation
          0x0000000000010000,   // length
          , , , TypeTranslation)
      })

      // _OSC: PCI Firmware spec 3.x sec. 4.5.1 (UUID 33DB4D5B-1FF7-401C-9657-7441C03DD766), ACPI sec. 6.2.11.
      // DWORD1 = status (bit 1 failure, bit 2 unrecognized UUID, bit 3 unrecognized revision, bit 4
      // capabilities masked), DWORD2 = supported, DWORD3 = control. Granted: PME (bit 2), AER (bit 3),
      // PCIe capability structure (bit 4), LTR (bit 5). Native hot plug (bits 0, 1) is not granted: the
      // bridge has no hot plug controller described here.
      Method (_OSC, 4, Serialized)
      {
        CreateDWordField (Arg3, 0, CDW1)
        If (Arg0 == ToUUID ("33DB4D5B-1FF7-401C-9657-7441C03DD766"))
        {
          CreateDWordField (Arg3, 4, CDW2)
          CreateDWordField (Arg3, 8, CDW3)
          If (Arg1 != 1)
          {
            CDW1 |= 0x08          // revision not supported
          }
          Local0 = CDW3
          CDW3 &= 0x3C            // grant at most PME, AER, PCIe capability, LTR
          If (Local0 != CDW3)
          {
            CDW1 |= 0x10          // some requested control bits were masked
          }
        }
        Else
        {
          CDW1 |= 0x04            // unrecognized UUID
        }
        Return (Arg3)
      }

      // _PRT (ACPI sec. 6.2.13): INTA-INTD of slots 0-31 routed straight to GSIs (Source = 0, so the
      // fourth member is the Global System Interrupt number). GSI = 35 + ((slot + pin) mod 4).
      Name (_PRT, Package ()
      {
        Package () { 0x0000FFFF, 0, 0, 35 },
        Package () { 0x0000FFFF, 1, 0, 36 },
        Package () { 0x0000FFFF, 2, 0, 37 },
        Package () { 0x0000FFFF, 3, 0, 38 },
        Package () { 0x0001FFFF, 0, 0, 36 },
        Package () { 0x0001FFFF, 1, 0, 37 },
        Package () { 0x0001FFFF, 2, 0, 38 },
        Package () { 0x0001FFFF, 3, 0, 35 },
        Package () { 0x0002FFFF, 0, 0, 37 },
        Package () { 0x0002FFFF, 1, 0, 38 },
        Package () { 0x0002FFFF, 2, 0, 35 },
        Package () { 0x0002FFFF, 3, 0, 36 },
        Package () { 0x0003FFFF, 0, 0, 38 },
        Package () { 0x0003FFFF, 1, 0, 35 },
        Package () { 0x0003FFFF, 2, 0, 36 },
        Package () { 0x0003FFFF, 3, 0, 37 },
        Package () { 0x0004FFFF, 0, 0, 35 },
        Package () { 0x0004FFFF, 1, 0, 36 },
        Package () { 0x0004FFFF, 2, 0, 37 },
        Package () { 0x0004FFFF, 3, 0, 38 },
        Package () { 0x0005FFFF, 0, 0, 36 },
        Package () { 0x0005FFFF, 1, 0, 37 },
        Package () { 0x0005FFFF, 2, 0, 38 },
        Package () { 0x0005FFFF, 3, 0, 35 },
        Package () { 0x0006FFFF, 0, 0, 37 },
        Package () { 0x0006FFFF, 1, 0, 38 },
        Package () { 0x0006FFFF, 2, 0, 35 },
        Package () { 0x0006FFFF, 3, 0, 36 },
        Package () { 0x0007FFFF, 0, 0, 38 },
        Package () { 0x0007FFFF, 1, 0, 35 },
        Package () { 0x0007FFFF, 2, 0, 36 },
        Package () { 0x0007FFFF, 3, 0, 37 },
        Package () { 0x0008FFFF, 0, 0, 35 },
        Package () { 0x0008FFFF, 1, 0, 36 },
        Package () { 0x0008FFFF, 2, 0, 37 },
        Package () { 0x0008FFFF, 3, 0, 38 },
        Package () { 0x0009FFFF, 0, 0, 36 },
        Package () { 0x0009FFFF, 1, 0, 37 },
        Package () { 0x0009FFFF, 2, 0, 38 },
        Package () { 0x0009FFFF, 3, 0, 35 },
        Package () { 0x000AFFFF, 0, 0, 37 },
        Package () { 0x000AFFFF, 1, 0, 38 },
        Package () { 0x000AFFFF, 2, 0, 35 },
        Package () { 0x000AFFFF, 3, 0, 36 },
        Package () { 0x000BFFFF, 0, 0, 38 },
        Package () { 0x000BFFFF, 1, 0, 35 },
        Package () { 0x000BFFFF, 2, 0, 36 },
        Package () { 0x000BFFFF, 3, 0, 37 },
        Package () { 0x000CFFFF, 0, 0, 35 },
        Package () { 0x000CFFFF, 1, 0, 36 },
        Package () { 0x000CFFFF, 2, 0, 37 },
        Package () { 0x000CFFFF, 3, 0, 38 },
        Package () { 0x000DFFFF, 0, 0, 36 },
        Package () { 0x000DFFFF, 1, 0, 37 },
        Package () { 0x000DFFFF, 2, 0, 38 },
        Package () { 0x000DFFFF, 3, 0, 35 },
        Package () { 0x000EFFFF, 0, 0, 37 },
        Package () { 0x000EFFFF, 1, 0, 38 },
        Package () { 0x000EFFFF, 2, 0, 35 },
        Package () { 0x000EFFFF, 3, 0, 36 },
        Package () { 0x000FFFFF, 0, 0, 38 },
        Package () { 0x000FFFFF, 1, 0, 35 },
        Package () { 0x000FFFFF, 2, 0, 36 },
        Package () { 0x000FFFFF, 3, 0, 37 },
        Package () { 0x0010FFFF, 0, 0, 35 },
        Package () { 0x0010FFFF, 1, 0, 36 },
        Package () { 0x0010FFFF, 2, 0, 37 },
        Package () { 0x0010FFFF, 3, 0, 38 },
        Package () { 0x0011FFFF, 0, 0, 36 },
        Package () { 0x0011FFFF, 1, 0, 37 },
        Package () { 0x0011FFFF, 2, 0, 38 },
        Package () { 0x0011FFFF, 3, 0, 35 },
        Package () { 0x0012FFFF, 0, 0, 37 },
        Package () { 0x0012FFFF, 1, 0, 38 },
        Package () { 0x0012FFFF, 2, 0, 35 },
        Package () { 0x0012FFFF, 3, 0, 36 },
        Package () { 0x0013FFFF, 0, 0, 38 },
        Package () { 0x0013FFFF, 1, 0, 35 },
        Package () { 0x0013FFFF, 2, 0, 36 },
        Package () { 0x0013FFFF, 3, 0, 37 },
        Package () { 0x0014FFFF, 0, 0, 35 },
        Package () { 0x0014FFFF, 1, 0, 36 },
        Package () { 0x0014FFFF, 2, 0, 37 },
        Package () { 0x0014FFFF, 3, 0, 38 },
        Package () { 0x0015FFFF, 0, 0, 36 },
        Package () { 0x0015FFFF, 1, 0, 37 },
        Package () { 0x0015FFFF, 2, 0, 38 },
        Package () { 0x0015FFFF, 3, 0, 35 },
        Package () { 0x0016FFFF, 0, 0, 37 },
        Package () { 0x0016FFFF, 1, 0, 38 },
        Package () { 0x0016FFFF, 2, 0, 35 },
        Package () { 0x0016FFFF, 3, 0, 36 },
        Package () { 0x0017FFFF, 0, 0, 38 },
        Package () { 0x0017FFFF, 1, 0, 35 },
        Package () { 0x0017FFFF, 2, 0, 36 },
        Package () { 0x0017FFFF, 3, 0, 37 },
        Package () { 0x0018FFFF, 0, 0, 35 },
        Package () { 0x0018FFFF, 1, 0, 36 },
        Package () { 0x0018FFFF, 2, 0, 37 },
        Package () { 0x0018FFFF, 3, 0, 38 },
        Package () { 0x0019FFFF, 0, 0, 36 },
        Package () { 0x0019FFFF, 1, 0, 37 },
        Package () { 0x0019FFFF, 2, 0, 38 },
        Package () { 0x0019FFFF, 3, 0, 35 },
        Package () { 0x001AFFFF, 0, 0, 37 },
        Package () { 0x001AFFFF, 1, 0, 38 },
        Package () { 0x001AFFFF, 2, 0, 35 },
        Package () { 0x001AFFFF, 3, 0, 36 },
        Package () { 0x001BFFFF, 0, 0, 38 },
        Package () { 0x001BFFFF, 1, 0, 35 },
        Package () { 0x001BFFFF, 2, 0, 36 },
        Package () { 0x001BFFFF, 3, 0, 37 },
        Package () { 0x001CFFFF, 0, 0, 35 },
        Package () { 0x001CFFFF, 1, 0, 36 },
        Package () { 0x001CFFFF, 2, 0, 37 },
        Package () { 0x001CFFFF, 3, 0, 38 },
        Package () { 0x001DFFFF, 0, 0, 36 },
        Package () { 0x001DFFFF, 1, 0, 37 },
        Package () { 0x001DFFFF, 2, 0, 38 },
        Package () { 0x001DFFFF, 3, 0, 35 },
        Package () { 0x001EFFFF, 0, 0, 37 },
        Package () { 0x001EFFFF, 1, 0, 38 },
        Package () { 0x001EFFFF, 2, 0, 35 },
        Package () { 0x001EFFFF, 3, 0, 36 },
        Package () { 0x001FFFFF, 0, 0, 38 },
        Package () { 0x001FFFFF, 1, 0, 35 },
        Package () { 0x001FFFFF, 2, 0, 36 },
        Package () { 0x001FFFFF, 3, 0, 37 }
      })
    }

    // The ECAM region as a motherboard resource (PCI Firmware spec 3.x sec. 4.1.2, PNP0C02): keeps the OS
    // from placing other devices over the configuration space.
    Device (RES0)
    {
      Name (_HID, EisaId ("PNP0C02"))
      Name (_UID, 0)
      Name (_CRS, ResourceTemplate ()
      {
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
          0x0000000000000000,
          0x0000004010000000,
          0x000000401FFFFFFF,
          0x0000000000000000,
          0x0000000010000000)
      })
    }
  }
}
