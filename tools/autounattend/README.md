# Installazione automatica di Windows 11 ARM64 (VM di test)

`autounattend.xml` pilota Setup senza interazione. Va nella radice di un CD (`unattend.iso`), che Setup cerca su tutti i supporti.
Rigenerare l'ISO: `tools/make-unattend-iso.sh` (crea `$WATARI_DATA/vm/unattend.iso`). Avvio: `tools/run-qemu-windows.sh --unattend`.

Fonte: "Unattended Windows Setup Reference" su learn.microsoft.com. Su ARM64 ogni componente usa `processorArchitecture="arm64"`.

## Sezioni
- **windowsPE**: `International-Core-WinPE` (it-IT, tastiera 0410:00000410); `Microsoft-Windows-Setup`:
  - `RunSynchronous`: `HKLM\SYSTEM\Setup\LabConfig` `BypassTPMCheck`, `BypassSecureBootCheck`, `BypassRAMCheck` = 1 (**ipotesi**: chiavi non documentate da Microsoft).
  - `DiskConfiguration`: disco 0 GPT, EFI 260 MB FAT32, MSR 16 MB, Windows NTFS (resto); `ImageInstall` su disco 0, partizione 3.
  - `AcceptEula`; `ProductKey` = GVLK di Windows 11 Pro (`W269N-WFGWX-YVC9B-4J6C9-T83GX`, pubblicata da Microsoft in "KMS client activation and product keys"): serve solo a scegliere l'edizione, non attiva. **Ipotesi**: accettata dall'ISO 26300. Se Setup chiede comunque l'edizione, sostituire il blocco `ProductKey` con `ImageInstall/OSImage/InstallFrom/MetaData` (`Key=/IMAGE/INDEX`, `Value=<n>`, indice letto con DISM da `install.wim`).
- **specialize**: nome computer `WATARI-VM`; `BypassNRO` in `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\OOBE` (**ipotesi**: non documentato; scelto qui come da richiesta, in alcune build serve in oobeSystem/`oobe\bypassnro`).
- **oobeSystem**: account locale amministratore, OOBE ridotta (`HideEULAPage`, `HideOnlineAccountScreens`, `HideWirelessSetupInOOBE`, `HideLocalAccountScreen`, `ProtectYourPC=3`), AutoLogon 1 volta; `FirstLogonCommands`: `bcdedit /set testsigning on`, `/debug on`, `/dbgsettings serial debugport:1 baudrate:115200`, e `C:\watari-ok.txt` con data e ora (prova di fine installazione).

## Credenziali di test (solo VM, non usare altrove)
Utente `watari`, password `watari`.

## Ipotesi aperte
Tutte da verificare alla prima installazione: LabConfig, BypassNRO, accettazione della GVLK, `HideLocalAccountScreen` su build 26300.
