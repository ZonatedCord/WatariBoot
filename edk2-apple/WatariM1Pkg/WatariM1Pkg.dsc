## @file
#  Watari Boot: EDK2 platform for the Apple M1 (j293) at EL1 on top of Watari EL2.
#
#  A FLAT description (it does not !include ArmVirtPkg/ArmVirt.dsc.inc): that file hard-wires
#  virtio, PCI, SCSI and USB components behind defines it sets itself from PLAT_XEN only, and puts
#  HighMemDxe under ENABLE_VIRTIO, so overriding it would mean either keeping modules this platform
#  must not contain or lying with PLAT_XEN [verified: reading ArmVirt.dsc.inc, edk2-stable202608].
#  The structure follows ArmVirtQemuKernel.dsc (relocatable uni-core PrePi entered like a Linux
#  arm64 Image) and ArmVirtXen.dsc (the existing minimal ArmVirt platform without PCI or fw_cfg).
#  Everything fw_cfg, virtio, PCI, NOR flash, network, TPM, PL031 and (outside the bench) PL011 is
#  left out. Design: docs/design/watari-edk2-m1/ .
#
#  Build options, all with tools/build-edk2-m1.sh:
#    -D WATARI_M1_BENCH_PL011=TRUE   QEMU bench: serial and debug through the PL011 that QEMU's
#                                    device tree describes (never in a production build). Without
#                                    it the console is the hypercall of Watari EL2 (E8).
#    -D WATARI_M1_ACPIGEN_MODEL=m    acpigen model of the ACPI tables: j293 (default) or qemu-virt.
#    -D WATARI_M1_ACPIVIEW=TRUE      add the Shell's acpiview command (bench).
#    -D WATARI_M1_SHELL_BOOT=TRUE    boot the UEFI Shell by default (bench). Without it the boot
#                                    manager starts the built-in self-test when nothing is bootable.
#    -D WATARI_M1_BENCH_RAMDISK=TRUE a RAM disk over a disk image the QEMU bench loaded into RAM, and a
#                                    stand-in boot application for it (bench, T16a; never in production).
#    -D WATARI_FB_REPORT_BGR8=TRUE   report the 10-bit panel as 8-bit BGR (untruthful, E11/DESIGN 8.5).
#
#  Copyright (c) 2026, the Watari Boot authors. All rights reserved.<BR>
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = WatariM1
  PLATFORM_GUID                  = 9e4c27b1-6a83-4d05-b1f7-2c58a0d3e694
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x0001001A
  OUTPUT_DIRECTORY               = Build/WatariM1-AArch64
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = WatariM1Pkg/WatariM1Pkg.fdf

  DEFINE WATARI_M1_BENCH_PL011   = FALSE
  DEFINE WATARI_M1_ACPIGEN_MODEL = j293
  DEFINE WATARI_M1_ACPIVIEW      = FALSE
  DEFINE WATARI_M1_SHELL_BOOT    = FALSE
  DEFINE WATARI_M1_BENCH_RAMDISK = FALSE
  DEFINE WATARI_FB_REPORT_BGR8   = FALSE

  # Shell: useful for the bench and for the RAM-disk step; the build script reports it.
  DEFINE BUILD_SHELL             = TRUE
  DEFINE SECURE_BOOT_ENABLE      = FALSE
  # ShellComponents.dsc.inc / ShellDxe.fdf.inc test these.
  DEFINE NETWORK_ENABLE          = FALSE
  DEFINE NETWORK_IP4_ENABLE      = FALSE
  DEFINE NETWORK_IP6_ENABLE      = FALSE
  DEFINE ACPIVIEW_ENABLE         = $(WATARI_M1_ACPIVIEW)

  DEFINE DEBUG_PRINT_ERROR_LEVEL = 0x8000004F

  # The image is loaded like a kernel: 3 MB, as the ArmVirt kernel variant.
  DEFINE FD_SIZE_IN_MB           = 3
  DEFINE FD_SIZE                 = 0x300000
  DEFINE FD_NUM_BLOCKS           = 0x300

# This comes at the beginning of includes to pick all relevant defines early on.
!include ArmVirtPkg/ArmVirtStackCookies.dsc.inc

!include MdePkg/MdeLibs.dsc.inc

[BuildOptions.common.EDKII.DXE_CORE,BuildOptions.common.EDKII.DXE_DRIVER,BuildOptions.common.EDKII.UEFI_DRIVER,BuildOptions.common.EDKII.UEFI_APPLICATION]
  GCC:*_GCC_*_DLINK_FLAGS = -z common-page-size=0x1000
  GCC:*_CLANGDWARF_*_DLINK_FLAGS = -z common-page-size=0x1000
  CLANGPDB:*_*_*_DLINK_FLAGS = /ALIGN:0x1000

[BuildOptions.common.EDKII.DXE_RUNTIME_DRIVER]
  GCC:*_GCC_*_DLINK_FLAGS = -z common-page-size=0x10000
  GCC:*_CLANGDWARF_*_DLINK_FLAGS = -z common-page-size=0x10000
  CLANGPDB:*_*_*_DLINK_FLAGS = /ALIGN:0x10000

[BuildOptions]
  GCC:RELEASE_*_*_CC_FLAGS  = -DMDEPKG_NDEBUG
  # Disable deprecated APIs.
  GCC:*_*_*_CC_FLAGS = -DDISABLE_NEW_DEPRECATED_INTERFACES
  # No jump tables in SEC modules, so that the PE/COFF self-relocation code itself is
  # guaranteed to be position independent.
  GCC:*_*_*_CC_FLAGS = -fno-jump-tables

################################################################################
#
# Library classes
#
################################################################################
[LibraryClasses.common]
  GptLib|MdeModulePkg/Library/GptLib/GptLib.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf

  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  SafeIntLib|MdePkg/Library/BaseSafeIntLib/BaseSafeIntLib.inf
  BmpSupportLib|MdeModulePkg/Library/BaseBmpSupportLib/BaseBmpSupportLib.inf
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  PerformanceLib|MdePkg/Library/BasePerformanceLibNull/BasePerformanceLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsicArmVirt.inf
  UefiDecompressLib|MdePkg/Library/BaseUefiDecompressLib/BaseUefiDecompressLib.inf
  CpuLib|MdePkg/Library/BaseCpuLib/BaseCpuLib.inf
  ImagePropertiesRecordLib|MdeModulePkg/Library/ImagePropertiesRecordLib/ImagePropertiesRecordLib.inf

  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  HobLib|ArmVirtPkg/Library/ArmVirtDxeHobLib/ArmVirtDxeHobLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLibDevicePathProtocol/UefiDevicePathLibDevicePathProtocol.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  DxeServicesLib|MdePkg/Library/DxeServicesLib/DxeServicesLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf

  MemDebugLogLib|OvmfPkg/Library/MemDebugLogLib/MemDebugLogLibNull.inf
!include OvmfPkg/Include/Dsc/ShellLibs.dsc.inc

  ShellCEntryLib|ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf
  FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf

  UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
  OrderedCollectionLib|MdePkg/Library/BaseOrderedCollectionRedBlackTreeLib/BaseOrderedCollectionRedBlackTreeLib.inf

  # RAM disk requirements
  FileExplorerLib|MdeModulePkg/Library/FileExplorerLib/FileExplorerLib.inf
  FrameBufferBltLib|MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.inf

  # Allow dynamic PCDs
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf

  # use the accelerated BaseMemoryLibOptDxe by default, overrides for SEC/PEI below
  BaseMemoryLib|MdePkg/Library/BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf

  # ARM architectural libraries
  ArmLib|MdePkg/Library/ArmLib/ArmBaseLib.inf
  ArmMmuLib|UefiCpuPkg/Library/ArmMmuLib/ArmMmuBaseLib.inf
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf
  CpuExceptionHandlerLib|UefiCpuPkg/Library/CpuExceptionHandlerLib/DxeCpuExceptionHandlerLib.inf
  ArmSmcLib|MdePkg/Library/ArmSmcLib/ArmSmcLib.inf
  ArmHvcLib|ArmPkg/Library/ArmHvcLib/ArmHvcLib.inf
  ArmGenericTimerCounterLib|ArmPkg/Library/ArmGenericTimerVirtCounterLib/ArmGenericTimerVirtCounterLib.inf
  TimerLib|ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.inf

  # Misc
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf
  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf
  CustomizedDisplayLib|MdeModulePkg/Library/CustomizedDisplayLib/CustomizedDisplayLib.inf
  TpmMeasurementLib|MdeModulePkg/Library/TpmMeasurementLibNull/TpmMeasurementLibNull.inf
  PlatformBootManagerLib|WatariM1Pkg/Library/WatariM1BootManagerLib/WatariM1BootManagerLib.inf

  # The platform libraries of this package (E1/E2).
  ArmVirtMemInfoLib|WatariM1Pkg/Library/WatariM1MemInfoLib/WatariM1MemInfoLib.inf
  MemoryInitPeiLib|ArmVirtPkg/Library/ArmVirtMemoryInitPeiLib/ArmVirtMemoryInitPeiLib.inf
  ResetSystemLib|ArmPkg/Library/ArmPsciResetSystemLib/ArmPsciResetSystemLib.inf
  ArmMonitorLib|ArmVirtPkg/Library/ArmVirtMonitorLib/ArmVirtMonitorLib.inf

  # Clock: virtual (a counter from the build time plus the generic timer), no PL031.
  RealTimeClockLib|EmbeddedPkg/Library/VirtualRealTimeClockLib/VirtualRealTimeClockLib.inf
  TimeBaseLib|EmbeddedPkg/Library/TimeBaseLib/TimeBaseLib.inf

  PeCoffExtraActionLib|ArmPkg/Library/DebugPeCoffExtraActionLib/DebugPeCoffExtraActionLib.inf
  DebugAgentLib|MdeModulePkg/Library/DebugAgentLibNull/DebugAgentLibNull.inf
  DebugAgentTimerLib|EmbeddedPkg/Library/DebugAgentTimerLibNull/DebugAgentTimerLibNull.inf

  # Flattened Device Tree (FDT) access library
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf

  # CryptoPkg libraries (resolution only; no module of this platform needs them yet)
  IntrinsicLib|CryptoPkg/Library/IntrinsicLib/IntrinsicLib.inf
  OpensslLib|CryptoPkg/Library/OpensslLib/OpensslLibCrypto.inf
  RngLib|MdePkg/Library/BaseRngLib/BaseRngLib.inf
  ArmTrngLib|ArmPkg/Library/ArmTrngLib/ArmTrngLib.inf

  AuthVariableLib|MdeModulePkg/Library/AuthVariableLibNull/AuthVariableLibNull.inf
  VarCheckLib|MdeModulePkg/Library/VarCheckLib/VarCheckLib.inf
  VariableFlashInfoLib|MdeModulePkg/Library/BaseVariableFlashInfoLib/BaseVariableFlashInfoLib.inf
  VariablePolicyLib|MdeModulePkg/Library/VariablePolicyLib/VariablePolicyLib.inf
  VariablePolicyHelperLib|MdeModulePkg/Library/VariablePolicyHelperLib/VariablePolicyHelperLib.inf
  UefiBootManagerLib|MdeModulePkg/Library/UefiBootManagerLib/UefiBootManagerLib.inf

  ReportStatusCodeLib|MdePkg/Library/BaseReportStatusCodeLibNull/BaseReportStatusCodeLibNull.inf

  BaseCryptLib|CryptoPkg/Library/BaseCryptLib/BaseCryptLib.inf

  ArmCcaLib|ArmVirtPkg/Library/ArmCcaLibNull/ArmCcaLibNull.inf

!if $(WATARI_M1_BENCH_PL011) == TRUE
  #
  # BENCH ONLY: serial and debug through the PL011 that QEMU's device tree describes.
  #
  PL011UartLib|ArmPlatformPkg/Library/PL011UartLib/PL011UartLib.inf
  SerialPortLib|ArmVirtPkg/Library/FdtPL011SerialPortLib/FdtPL011SerialPortLib.inf
  FdtSerialPortAddressLib|OvmfPkg/Library/FdtSerialPortAddressLib/FdtSerialPortAddressLib.inf
  PlatformPeiLib|WatariM1Pkg/Library/WatariM1PlatformPeiLib/WatariM1PlatformPeiLibBench.inf
!if $(TARGET) == RELEASE
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!else
  DebugLib|ArmVirtPkg/Library/DebugLibFdtPL011Uart/DebugLibFdtPL011UartRam.inf
!endif
!else
  #
  # Production: no UART of any kind. The console is the Watari EL2 hypercall WEL2_CONSOLE_WRITE
  # (E8): SerialPortLib for every boot-time phase (PrePi/SEC included: the library needs no
  # memory service), DebugLib on top of it; runtime drivers get BaseDebugLibNull below.
  #
  SerialPortLib|WatariM1Pkg/Library/WatariHvcSerialPortLib/WatariHvcSerialPortLib.inf
  PlatformPeiLib|WatariM1Pkg/Library/WatariM1PlatformPeiLib/WatariM1PlatformPeiLib.inf
!if $(TARGET) == RELEASE
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!else
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
!endif
!endif

[LibraryClasses.common.SEC]
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf

  DebugAgentLib|ArmPkg/Library/DebugAgentSymbolsBaseLib/DebugAgentSymbolsBaseLib.inf
  HobLib|MdePkg/Library/PeiHobLib/PeiHobLib.inf
  PeiServicesLib|MdePkg/Library/PeiServicesLib/PeiServicesLib.inf
  PeiServicesTablePointerLib|ArmPkg/Library/PeiServicesTablePointerLib/PeiServicesTablePointerLib.inf
  MemoryAllocationLib|MdePkg/Library/PeiMemoryAllocationLib/PeiMemoryAllocationLib.inf
!if $(WATARI_M1_BENCH_PL011) == TRUE
  SerialPortLib|ArmVirtPkg/Library/FdtPL011SerialPortLib/EarlyFdtPL011SerialPortLib.inf
!if $(TARGET) != RELEASE
  DebugLib|ArmVirtPkg/Library/DebugLibFdtPL011Uart/DebugLibFdtPL011UartFlash.inf
!endif
!endif

  ArmCcaInitPeiLib|ArmVirtPkg/Library/ArmCcaInitPeiLibNull/ArmCcaInitPeiLibNull.inf

[LibraryClasses.common.PEI_CORE]
  PcdLib|MdePkg/Library/PeiPcdLib/PeiPcdLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  HobLib|MdePkg/Library/PeiHobLib/PeiHobLib.inf
  PeiServicesLib|MdePkg/Library/PeiServicesLib/PeiServicesLib.inf
  MemoryAllocationLib|MdePkg/Library/PeiMemoryAllocationLib/PeiMemoryAllocationLib.inf
  PeiCoreEntryPoint|MdePkg/Library/PeiCoreEntryPoint/PeiCoreEntryPoint.inf
  PerformanceLib|MdeModulePkg/Library/PeiPerformanceLib/PeiPerformanceLib.inf
  OemHookStatusCodeLib|MdeModulePkg/Library/OemHookStatusCodeLibNull/OemHookStatusCodeLibNull.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  ExtractGuidedSectionLib|MdePkg/Library/PeiExtractGuidedSectionLib/PeiExtractGuidedSectionLib.inf

  PeiServicesTablePointerLib|ArmPkg/Library/PeiServicesTablePointerLib/PeiServicesTablePointerLib.inf
!if $(WATARI_M1_BENCH_PL011) == TRUE
  SerialPortLib|ArmVirtPkg/Library/FdtPL011SerialPortLib/EarlyFdtPL011SerialPortLib.inf
!if $(TARGET) != RELEASE
  DebugLib|ArmVirtPkg/Library/DebugLibFdtPL011Uart/DebugLibFdtPL011UartFlash.inf
!endif
!endif

[LibraryClasses.common.PEIM]
  PcdLib|MdePkg/Library/PeiPcdLib/PeiPcdLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  HobLib|MdePkg/Library/PeiHobLib/PeiHobLib.inf
  PeiServicesLib|MdePkg/Library/PeiServicesLib/PeiServicesLib.inf
  MemoryAllocationLib|MdePkg/Library/PeiMemoryAllocationLib/PeiMemoryAllocationLib.inf
  PeimEntryPoint|MdePkg/Library/PeimEntryPoint/PeimEntryPoint.inf
  PerformanceLib|MdeModulePkg/Library/PeiPerformanceLib/PeiPerformanceLib.inf
  OemHookStatusCodeLib|MdeModulePkg/Library/OemHookStatusCodeLibNull/OemHookStatusCodeLibNull.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  ExtractGuidedSectionLib|MdePkg/Library/PeiExtractGuidedSectionLib/PeiExtractGuidedSectionLib.inf

  PeiServicesTablePointerLib|ArmPkg/Library/PeiServicesTablePointerLib/PeiServicesTablePointerLib.inf
!if $(WATARI_M1_BENCH_PL011) == TRUE
  SerialPortLib|ArmVirtPkg/Library/FdtPL011SerialPortLib/EarlyFdtPL011SerialPortLib.inf
!if $(TARGET) != RELEASE
  DebugLib|ArmVirtPkg/Library/DebugLibFdtPL011Uart/DebugLibFdtPL011UartFlash.inf
!endif
!endif

[LibraryClasses.common.UEFI_DRIVER]
  # resolve RngLib via RngDxe's EFI_RNG_PROTOCOL for UEFI_DRIVER type drivers
  RngLib|MdePkg/Library/DxeRngLib/DxeRngLib.inf
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf
  PerformanceLib|MdeModulePkg/Library/DxePerformanceLib/DxePerformanceLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf

[LibraryClasses.common.DXE_CORE]
  HobLib|MdePkg/Library/DxeCoreHobLib/DxeCoreHobLib.inf
  MemoryAllocationLib|MdeModulePkg/Library/DxeCoreMemoryAllocationLib/DxeCoreMemoryAllocationLib.inf
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf
  PerformanceLib|MdeModulePkg/Library/DxeCorePerformanceLib/DxeCorePerformanceLib.inf

[LibraryClasses.common.DXE_DRIVER]
  SecurityManagementLib|MdeModulePkg/Library/DxeSecurityManagementLib/DxeSecurityManagementLib.inf
  PerformanceLib|MdeModulePkg/Library/DxePerformanceLib/DxePerformanceLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  MapMmioLib|OvmfPkg/Library/MapMmioLib/MapMmioLib.inf
  ReportStatusCodeLib|MdeModulePkg/Library/DxeReportStatusCodeLib/DxeReportStatusCodeLib.inf

[LibraryClasses.common.UEFI_APPLICATION]
  PerformanceLib|MdeModulePkg/Library/DxePerformanceLib/DxePerformanceLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  MapMmioLib|OvmfPkg/Library/MapMmioLib/MapMmioLib.inf
!if $(TARGET) != RELEASE
!if $(WATARI_M1_BENCH_PL011) == TRUE
  DebugLib|ArmVirtPkg/Library/DebugLibFdtPL011Uart/DxeRuntimeDebugLibFdtPL011Uart.inf
!else
  # After SetVirtualAddressMap a buffer address is no longer a guest physical address, so a
  # runtime driver cannot use the hypercall console: no debug output from runtime drivers.
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!endif
!endif
  VariablePolicyLib|MdeModulePkg/Library/VariablePolicyLib/VariablePolicyLibRuntimeDxe.inf

  BaseCryptLib|CryptoPkg/Library/BaseCryptLib/RuntimeCryptLib.inf

################################################################################
#
# Pcd Section
#
################################################################################

[PcdsFeatureFlag.common]
  gEfiMdeModulePkgTokenSpaceGuid.PcdHiiOsRuntimeSupport|FALSE
  gEmbeddedTokenSpaceGuid.PcdPrePiProduceMemoryTypeInformationHob|TRUE
  # Activate AcpiSdtProtocol (acpiview and the like).
  gEfiMdeModulePkgTokenSpaceGuid.PcdInstallAcpiSdtProtocol|TRUE
  # Graphics Output Protocol on the virtual handle created by ConSplitterDxe.
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutGopSupport|TRUE

[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdMonitorConduitHvc|TRUE

  gEfiMdePkgTokenSpaceGuid.PcdMaximumUnicodeStringLength|1000000
  gEfiMdePkgTokenSpaceGuid.PcdMaximumAsciiStringLength|1000000
  gEfiMdePkgTokenSpaceGuid.PcdMaximumLinkedListLength|0
  gEfiMdePkgTokenSpaceGuid.PcdSpinLockTimeout|10000000
  gEfiMdePkgTokenSpaceGuid.PcdUefiLibMaxPrintBufferSize|320

  # DEBUG_ASSERT_ENABLED 0x01, DEBUG_PRINT_ENABLED 0x02, DEBUG_CODE_ENABLED 0x04,
  # CLEAR_MEMORY_ENABLED 0x08, ASSERT_BREAKPOINT_ENABLED 0x10, ASSERT_DEADLOOP_ENABLED 0x20
!if $(TARGET) == RELEASE
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x21
!else
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2f
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|$(DEBUG_PRINT_ERROR_LEVEL)
!endif

  # EFI memory map fragmentation control (see ArmVirt.dsc.inc for the rationale)
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiACPIReclaimMemory|4
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiACPIMemoryNVS|0x3C
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiReservedMemoryType|0
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesData|300
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesCode|0x424
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesCode|1000
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesData|12000
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderCode|20
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderData|0

  # Strict image permissions and NX for all non-code regions.
  gEfiMdeModulePkgTokenSpaceGuid.PcdImageProtectionPolicy|0x3
  gEfiMdeModulePkgTokenSpaceGuid.PcdDxeNxMemoryProtectionPolicy|0xC000000000007FD5
  gEfiMdeModulePkgTokenSpaceGuid.PcdCpuStackGuard|TRUE

  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x4000
  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxVariableSize|0x2000
  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxAuthVariableSize|0x2800

  # Size of the region used by UEFI in permanent memory (reserved 64 MB)
  gArmPlatformTokenSpaceGuid.PcdSystemMemoryUefiRegionSize|0x04000000

  # Variables: RAM only, lost at reset (DESIGN 9.3): emulated non-volatile mode of VariableRuntimeDxe.
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE

  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|38400
  ## Default Terminal Type: 0-PCANSI, 1-VT100, 2-VT100+, 3-UTF8, 4-TTYTERM
  gEfiMdePkgTokenSpaceGuid.PcdDefaultTerminalType|4

!if $(WATARI_M1_BENCH_PL011) == TRUE
  # QEMU virt: PL011 at 0x09000000 (the only peripheral the PEI MMU map gets; the GIC is mapped on demand).
  gWatariM1TokenSpaceGuid.PcdWatariM1BenchUartBase|0x09000000
!endif
!if $(WATARI_M1_BENCH_RAMDISK) == TRUE
  gWatariM1TokenSpaceGuid.PcdWatariM1BenchRamDiskBase|0xB0000000
  gWatariM1TokenSpaceGuid.PcdWatariM1BenchRamDiskSize|0x1000000
!endif

  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE

  # E11: report the 10-bit panel as 8-bit BGR (build define WATARI_FB_REPORT_BGR8, default FALSE).
  gWatariM1TokenSpaceGuid.PcdWatariFbReportBgr8|$(WATARI_FB_REPORT_BGR8)

  # E12: SMBIOS 3.0, entry point 3.0 only (as ArmVirtQemu).
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion|0x0300
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosDocRev|0x0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosEntryPointProvideMethod|0x2
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x21, 0xaa, 0x2c, 0x46, 0x14, 0x76, 0x03, 0x45, 0x83, 0x6e, 0x8a, 0xb6, 0xf4, 0x66, 0x23, 0x31 }

  # The maximum physical I/O addressability of the processor, set with BuildCpuHob().
  gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|16

  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|3
  gEfiShellPkgTokenSpaceGuid.PcdShellFileOperationSize|0x20000

!if $(WATARI_M1_SHELL_BOOT) == TRUE
  gArmTokenSpaceGuid.PcdUefiShellDefaultBootEnable|TRUE
!endif

[PcdsPatchableInModule.common]
  # Set by PrePi from the first /memory range of the guest FDT.
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x0
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x0

  # Initial address of the device tree. Ignored when x0 != 0 at entry (the M1 layer passes x0);
  # the bench default (QEMU convention, F-21) is the RAM base, because the PL011 debug library reads
  # the FDT there before PrePi knows x0.
!if $(WATARI_M1_BENCH_PL011) == TRUE
  gUefiOvmfPkgTokenSpaceGuid.PcdDeviceTreeInitialBaseAddress|0x40000000
!else
  gUefiOvmfPkgTokenSpaceGuid.PcdDeviceTreeInitialBaseAddress|0x0
!endif

  gArmTokenSpaceGuid.PcdFdBaseAddress|0x0
  gArmTokenSpaceGuid.PcdFvBaseAddress|0x0

[PcdsDynamicDefault.common]
  # E12: never wait for a key (DESIGN 10.3).
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|0

  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|0x0
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|0x0
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|0x0
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|0x0
  gArmTokenSpaceGuid.PcdArmArchTimerHypVirtIntrNum|0x0

  # ARM General Interrupt Controller (filled in from the FDT by ArmVirtGicArchLib)
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x0
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x0
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x0

  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|0

################################################################################
#
# Components Section
#
################################################################################
[Components.common]
  #
  # PEI phase: the relocatable uni-core PrePi
  #
  ArmVirtPkg/PrePi/ArmVirtPrePiUniCoreRelocatable.inf {
    <LibraryClasses>
      ExtractGuidedSectionLib|EmbeddedPkg/Library/PrePiExtractGuidedSectionLib/PrePiExtractGuidedSectionLib.inf
      NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
      PrePiLib|EmbeddedPkg/Library/PrePiLib/PrePiLib.inf
      HobLib|EmbeddedPkg/Library/PrePiHobLib/PrePiHobLib.inf
      PrePiHobListPointerLib|ArmPlatformPkg/Library/PrePiHobListPointerLib/PrePiHobListPointerLib.inf
      MemoryAllocationLib|EmbeddedPkg/Library/PrePiMemoryAllocationLib/PrePiMemoryAllocationLib.inf
  }

  #
  # DXE core and architectural protocols
  #
  MdeModulePkg/Core/Dxe/DxeMain.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DxeCrc32GuidedSectionExtractLib/DxeCrc32GuidedSectionExtractLib.inf
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  }
  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  }

  ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  MdeModulePkg/Universal/Metronome/Metronome.inf

  ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf {
    <LibraryClasses>
      NULL|ArmVirtPkg/Library/ArmVirtGicArchLib/ArmVirtGicArchLib.inf
  }
  ArmPkg/Drivers/TimerDxe/TimerDxe.inf {
    <LibraryClasses>
      NULL|ArmVirtPkg/Library/ArmVirtTimerFdtClientLib/ArmVirtTimerFdtClientLib.inf
  }

  # Variables (emulated NV mode, no flash) and clock (virtual)
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/VarCheckUefiLib/VarCheckUefiLib.inf
  }
  EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  SecurityPkg/RandomNumberGenerator/RngDxe/RngDxe.inf

  #
  # Platform: the device tree and the other RAM ranges
  #
  EmbeddedPkg/Drivers/FdtClientDxe/FdtClientDxe.inf
  OvmfPkg/Fdt/HighMemDxe/HighMemDxe.inf

  #
  # Console and file systems (a RAM disk and the Shell for the first steps)
  #
  MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf
  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf
  MdeModulePkg/Universal/Disk/UdfDxe/UdfDxe.inf
  MdeModulePkg/Universal/Disk/RamDiskDxe/RamDiskDxe.inf

  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf

!include OvmfPkg/Include/Dsc/ShellComponents.dsc.inc

  #
  # Bds
  #
  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf {
    <LibraryClasses>
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  }
  MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  MdeModulePkg/Application/UiApp/UiApp.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DeviceManagerUiLib/DeviceManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootManagerUiLib/BootManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerUiLib.inf
  }
  MdeModulePkg/Universal/DriverHealthManagerDxe/DriverHealthManagerDxe.inf

  #
  # ACPI: the table protocol, the tables of this platform and the DSDT (E4). AcpiTableDxe is built
  # WITHOUT PlatformHasAcpiLib: that library makes it wait for a "platform has ACPI" protocol that
  # only a driver depending on AcpiTableDxe would install; this platform always has ACPI.
  #
  MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
  WatariM1Pkg/AcpiTables/Dsdt.inf
  WatariM1Pkg/Drivers/WatariM1AcpiDxe/WatariM1AcpiDxe.inf {
!if "$(WATARI_M1_ACPIGEN_MODEL)" == "qemu-virt"
    <BuildOptions>
      GCC:*_*_*_CC_FLAGS = -DWATARI_M1_MODEL_QEMU_VIRT=1
!endif
  }

  #
  # Framebuffer (E11) and the first-boot self-test (E12). The self-test application is in the FV
  # and started by WatariM1BootManagerLib; SMBIOS has fixed strings and no serial number.
  #
  WatariM1Pkg/Drivers/WatariSimpleFbDxe/WatariSimpleFbDxe.inf
  WatariM1Pkg/Application/WatariM1SelfTest/WatariM1SelfTest.inf
  MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
  WatariM1Pkg/Drivers/WatariM1SmbiosDxe/WatariM1SmbiosDxe.inf
!if $(WATARI_M1_BENCH_RAMDISK) == TRUE
  WatariM1Pkg/Drivers/WatariM1BenchRamDiskDxe/WatariM1BenchRamDiskDxe.inf
  WatariM1Pkg/Application/WatariM1BenchBootTest/WatariM1BenchBootTest.inf
!endif
