; THOREAU installer (Inno Setup 6). Built by .github/workflows/build-windows.yml:
;   iscc /DAppVersion=1.0.12 Installer\THOREAU.iss
; Installs for the current user (no admin prompt), so THOREAU can update itself.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{6B3E2A7C-91F4-4D2B-A8C5-3F0E7D1B9A64}
AppName=THOREAU
AppVersion={#AppVersion}
AppVerName=THOREAU {#AppVersion}
AppPublisher=PEQUOD
DefaultDirName={autopf}\THOREAU
DefaultGroupName=THOREAU
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=THOREAU
UninstallDisplayIcon={app}\THOREAU.exe
OutputDir=..\dist
OutputBaseFilename=THOREAU-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=THOREAU.ico
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Put a THOREAU shortcut on the desktop"; GroupDescription: "Shortcuts:"

[Files]
Source: "..\build\Thoreau_artefacts\Release\THOREAU.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Resources\Fonts\*.txt"; DestDir: "{app}\Licences"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\THOREAU"; Filename: "{app}\THOREAU.exe"
Name: "{autodesktop}\THOREAU"; Filename: "{app}\THOREAU.exe"; Tasks: desktopicon

[Run]
; normal install: offer to start it; update from inside THOREAU (silent): start it again by itself
Filename: "{app}\THOREAU.exe"; Description: "Start THOREAU"; Flags: nowait postinstall skipifsilent
Filename: "{app}\THOREAU.exe"; Flags: nowait; Check: WizardSilent
