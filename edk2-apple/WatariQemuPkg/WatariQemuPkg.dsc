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
  DEFINE WATARI_TIMER_FIQ        = FALSE
  DEFINE WATARI_PARKING          = FALSE
  DEFINE WATARI_NOGICD           = FALSE
  DEFINE WATARI_PMODE            = 0

!include ArmVirtPkg/ArmVirtQemu.dsc

# ArmVirtQemu.dsc fissa FLASH_DEFINITION: va ridefinito DOPO l'include. WatariQemu.fdf non e' nel
# repo: lo genera tools/build-edk2-watari.sh nella radice del WORKSPACE (copia di ArmVirtQemu.fdf con
# il driver aggiunto a FvMain; le !include dell'FDF non sono annidabili tra pacchetti).
[Defines]
  FLASH_DEFINITION               = WatariQemu.fdf

[Components.AARCH64]
  WatariQemuPkg/WatariAcpiOverride/WatariAcpiOverride.inf

# V6 (metodo A): driver che porta i PPI del timer in Group 0 (FIQ). Attivo solo con -D WATARI_TIMER_FIQ=TRUE
# (tools/build-edk2-watari.sh --fiq); il firmware di default non cambia.
!if $(WATARI_TIMER_FIQ) == TRUE
[Components.AARCH64]
  WatariQemuPkg/WatariTimerFiqDxe/WatariTimerFiqDxe.inf
!endif

# G4/V3: parcheggio dei core secondari (Multiprocessor Parking Protocol) + MADT parking v1 + FADT senza PSCI.
# Attivo solo con -D WATARI_PARKING=TRUE (tools/build-edk2-watari.sh --parking).
!if $(WATARI_PARKING) == TRUE
[Components.AARCH64]
  WatariQemuPkg/WatariParkingDxe/WatariParkingDxe.inf

# Tentativo A (ultimo): maschera di bit per gli esperimenti sul 0x5C, vedi tools/build-edk2-watari.sh
# (WATARI_PMODE=n nell'ambiente). 1 FADT PSCI+HVC, 2 MADT senza parking, 4 niente CPU_ON, 8 stub senza GIC,
# 16 niente wake dei redistributor, 32 stub azzera i GPR.
[BuildOptions]
  GCC:*_*_AARCH64_CC_FLAGS = -DWATARI_PMODE=$(WATARI_PMODE)
!endif

# G2/V2: MADT senza GICD (modello qemu-virt-nogicd). Attivo solo con -D WATARI_NOGICD=TRUE
# (tools/build-edk2-watari.sh --nogicd).
!if $(WATARI_NOGICD) == TRUE
[BuildOptions]
  GCC:*_*_AARCH64_CC_FLAGS = -DWATARI_NOGICD=1
!endif
