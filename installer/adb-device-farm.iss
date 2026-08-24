; Inno Setup script for ADB Device Farm.
; Build the portable folder first:  scripts\package-portable.ps1
; then compile this script with Inno Setup 6 (free, https://jrsoftware.org/isinfo.php):
;   ISCC.exe installer\adb-device-farm.iss
; The installer copies the portable folder contents (Qt/FFmpeg/adb are all
; redistributable under their licenses, see docs\THIRD_PARTY.md) and creates
; Start-menu / desktop shortcuts that launch the farm (--farm).

#define AppName "ADB Device Farm"
#define AppVersion "3.0.0"
#define AppPublisher "ADB Device Farm contributors"
#define AppExe "QtScrcpy.exe"
#define SourceDir "..\dist\ADBDeviceFarm-portable-" + AppVersion

[Setup]
AppId={{7E3C0F8A-6B55-4C57-9C6E-ADBDEVICEFARM}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=..\dist
OutputBaseFilename=ADBDeviceFarm-setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
UninstallDisplayIcon={app}\{#AppExe}
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; everything from the portable build except the portable marker (installed copies use %APPDATA%)
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "portable,ADB Device Farm.cmd,*.zip"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; Parameters: "--farm"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Parameters: "--farm"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Parameters: "--farm"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
