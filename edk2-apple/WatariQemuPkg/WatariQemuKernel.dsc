## @file
#  Wrapper of ArmVirtPkg/ArmVirtQemuKernel.dsc (the firmware loaded as a "kernel" under Watari EL2, used by
#  el2/boot/run-m2.sh): adds WatariAcpiOverride for the E13 reduced-ACPI experiments (-D WATARI_E13=1|2).
#  Needs WATARI_ACPIGEN (tools/acpigen folder) and WATARI_E13INC (folder with qemu_virt_min.hex), both set
#  by tools/build-edk2-watari.sh --variant e13a|e13b.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = WatariQemuKernel
  PLATFORM_GUID                  = 5b1e6f42-9a07-4c3d-8e21-0f4d7a93c6b8
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x0001001A
  OUTPUT_DIRECTORY               = Build/WatariQemuKernel-AArch64
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT
  DEFINE WATARI_E13              = 1

!include ArmVirtPkg/ArmVirtQemuKernel.dsc

# FLASH_DEFINITION is set by the included DSC: redefine it AFTER the include. WatariQemuKernel.fdf is not in
# the repo: tools/build-edk2-watari.sh generates it in the WORKSPACE root from ArmVirtQemuKernel.fdf.
[Defines]
  FLASH_DEFINITION               = WatariQemuKernel.fdf

[Components.AARCH64]
  WatariQemuPkg/WatariAcpiOverride/WatariAcpiOverride.inf

[BuildOptions]
  GCC:*_*_AARCH64_CC_FLAGS = -DWATARI_E13=$(WATARI_E13) -I$(WATARI_E13INC)
