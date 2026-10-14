## @file
#  Wrapper di ArmVirtPkg/ArmVirtQemu.dsc: aggiunge WatariAcpiOverride senza toccare EDK2.
#  Richiede la variabile d'ambiente WATARI_ACPIGEN (cartella tools/acpigen), impostata da
#  tools/build-edk2-watari.sh.
#
#  SPDX-License-Identifier: MIT
##

[Defines]
  PLATFORM_NAME                  = WatariQemu
  PLATFORM_GUID                  = 0d3a7c18-52f4-4e0b-8b6e-9c1a4f7d2e55
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x0001001A
  OUTPUT_DIRECTORY               = Build/WatariQemu-AArch64
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

!include ArmVirtPkg/ArmVirtQemu.dsc

# ArmVirtQemu.dsc fissa FLASH_DEFINITION: va ridefinito DOPO l'include. WatariQemu.fdf non e' nel
# repo: lo genera tools/build-edk2-watari.sh nella radice del WORKSPACE (copia di ArmVirtQemu.fdf con
# il driver aggiunto a FvMain; le !include dell'FDF non sono annidabili tra pacchetti).
[Defines]
  FLASH_DEFINITION               = WatariQemu.fdf

[Components.AARCH64]
  WatariQemuPkg/WatariAcpiOverride/WatariAcpiOverride.inf
