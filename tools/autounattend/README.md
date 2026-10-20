# Automatic installation of Windows 11 ARM64 (test VM)

`autounattend.xml` drives Setup without interaction. It goes in the root of a CD (`unattend.iso`), which Setup looks for on all media.
Regenerate the ISO: `tools/make-unattend-iso.sh` (creates `$WATARI_DATA/vm/unattend.iso`). Launch: `tools/run-qemu-windows.sh --unattend`.

Source: "Unattended Windows Setup Reference" on learn.microsoft.com. On ARM64 every component uses `processorArchitecture="arm64"`.

## Sections
- **windowsPE**: `International-Core-WinPE` (it-IT, keyboard 0410:00000410); `Microsoft-Windows-Setup`:
  - `RunSynchronous`: `HKLM\SYSTEM\Setup\LabConfig` `BypassTPMCheck`, `BypassSecureBootCheck`, `BypassRAMCheck` = 1 (**hypothesis**: keys not documented by Microsoft).
  - `DiskConfiguration`: disk 0 GPT, EFI 260 MB FAT32, MSR 16 MB, Windows NTFS (rest); `ImageInstall` on disk 0, partition 3.
  - `AcceptEula`; `ProductKey` = GVLK of Windows 11 Pro (`W269N-WFGWX-YVC9B-4J6C9-T83GX`, published by Microsoft in "KMS client activation and product keys"): it only serves to choose the edition, it does not activate. **Hypothesis**: accepted by ISO 26300. If Setup still asks for the edition, replace the `ProductKey` block with `ImageInstall/OSImage/InstallFrom/MetaData` (`Key=/IMAGE/INDEX`, `Value=<n>`, index read with DISM from `install.wim`).
- **specialize**: computer name `WATARI-VM`; `BypassNRO` in `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\OOBE` (**hypothesis**: undocumented; chosen here as requested, on some builds it is needed in oobeSystem/`oobe\bypassnro`).
- **oobeSystem**: local administrator account, reduced OOBE (`HideEULAPage`, `HideOnlineAccountScreens`, `HideWirelessSetupInOOBE`, `HideLocalAccountScreen`, `ProtectYourPC=3`), AutoLogon 1 time; `FirstLogonCommands`: `bcdedit /set testsigning on`, `/debug on`, `/dbgsettings serial debugport:1 baudrate:115200`, and `C:\watari-ok.txt` with date and time (proof of installation end).

## Test credentials (VM only, do not use elsewhere)
User `watari`, password `watari`.

## Open hypotheses
All to be verified at the first installation: LabConfig, BypassNRO, acceptance of the GVLK, `HideLocalAccountScreen` on build 26300.

## Verified result (2026-10-20, issue #45)
Black-box check on the installed VM (HVF, stock EDK2 `QEMU_EFI`, `snapshot=on`, serial to file, keystrokes via the QEMU monitor; the VM keyboard layout is Italian). **[verified]**
- **Test signing: enabled.** `bcdedit /enum` shows `testsigning Yes`; the desktop shows the "Modalita test" watermark. Screenshot: `docs/img/vm-bcdedit-testsigning-debug.png`.
- **Kernel debugging: enabled over serial.** `bcdedit /enum` shows `debug Yes`; `bcdedit /dbgsettings` shows `debugtype Serial`, `debugport 1`, `baudrate 115200` (`docs/img/vm-dbgsettings-serial.png`). The QEMU serial log contains real KD packets (leader `0000`, `KDTARGET: Refreshing KD connection`, after `\SystemRoot\system32\ntoskrnl.exe`): the kernel debugger is active on COM1 and waits for a host debugger.
- **`C:\watari-ok.txt` exists**: `13/10/2026 23:36:47,41`.
- AutoLogon (LogonCount 1) had already been consumed: the reboot shows the lock screen and a `watari` password prompt (password `watari`). A cmd prompt needs elevation (UAC: Alt+S for "Si").
No change to `autounattend.xml` was needed.
