; mapsmith.iss - Windows installer (Inno Setup 6): one Mapsmith-Setup-<version>.exe that installs
; the editor for the current user (no administrator rights), adds Start menu and optional
; desktop shortcuts and an uninstaller. The built-in updater replaces the files in the same
; folder later, which works because the folder belongs to the user.
;
; Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.2.3 /DSourceDir=dist\Mapsmith-1.2.3-windows /DOutputDir=dist installer\mapsmith.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must point at the folder with Mapsmith.exe and its DLLs
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6E4C3A62-5F0B-4A8E-9C1D-2B7A9F4E8D31}
AppName=Mapsmith
AppVersion={#AppVersion}
AppPublisher=Mapsmith contributors
AppPublisherURL=https://github.com/29j2003/hammer-test
AppSupportURL=https://github.com/29j2003/hammer-test/issues
DefaultDirName={localappdata}\Programs\Mapsmith
DefaultGroupName=Mapsmith
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=Mapsmith-Setup-{#AppVersion}
SetupIconFile=..\src\app\resources\mapsmith.ico
UninstallDisplayIcon={app}\Mapsmith.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
InfoBeforeFile=..\THIRD_PARTY_NOTICES.txt

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Mapsmith"; Filename: "{app}\Mapsmith.exe"
Name: "{group}\Uninstall Mapsmith"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Mapsmith"; Filename: "{app}\Mapsmith.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Mapsmith.exe"; Description: "{cm:LaunchProgram,Mapsmith}"; Flags: nowait postinstall skipifsilent
