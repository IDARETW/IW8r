; IW8 Radiant Suite - Inno Setup 6 installer script (roadmap 9.9).
; Packages the folder built by tools\package_release.ps1 (out\package\IW8RadiantSuite).
; Build with tools\build_installer.ps1, or directly:
;   ISCC.exe /DSourceDir="E:\IW8-Radiant\out\package\IW8RadiantSuite" iw8radiant.iss
;
; Settings: the editor keeps IW8Radiant.ini beside IW8Radiant.exe (src\qtapp\settings.cpp), together with the
; optional web_tls\ certificate folder. Neither is part of the package and neither is listed in [Files], so Setup
; never writes, replaces or removes them: an upgrade into the same folder keeps them, and so does an uninstall.
; The uninstaller asks (default: No) whether to delete them as well.

#ifndef SourceDir
  #define SourceDir "..\..\out\package\IW8RadiantSuite"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\out\installer"
#endif
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#define MyAppName "IW8 Radiant Suite"
#define MyAppExe "IW8Radiant.exe"

[Setup]
; Fixed GUID: keep it across versions so upgrades replace the same installation.
AppId={{8E3C2B7A-5D41-4F9E-A6C0-3B1D7E2F9A54}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppName}
AppPublisherURL=https://github.com/
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
UsePreviousGroup=yes
; Per-user install by default (no administrator rights, %LOCALAPPDATA%\Programs); the wizard offers all-users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=IW8RadiantSuite-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppVersion}
UninstallDisplayIcon={app}\{#MyAppExe}
; The editor may be running during an upgrade: offer to close it instead of failing on locked files.
CloseApplications=yes
CloseApplicationsFilter=*.exe
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
; Everything out\package\IW8RadiantSuite holds. *.ini is excluded on purpose: a stray IW8Radiant.ini in the source
; folder must never overwrite the mapper's settings.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.ini"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "Launch {#MyAppName}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DeleteUserData: Boolean;

// The editor is built with the MSVC 2015-2022 runtime. package_release.ps1 does not bundle it; this only checks and
// tells the user where to get it. Setup continues either way.
function VCRedistInstalled(): Boolean;
var
  Installed: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed)
            and (Installed = 1);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if not VCRedistInstalled() then
    MsgBox('{#MyAppName} needs the Microsoft Visual C++ 2015-2022 x64 Redistributable, which was not found on this PC.' + #13#10 + #13#10 +
           'Install it from Microsoft (search "Visual C++ Redistributable latest supported downloads"), then start the editor.' + #13#10 +
           'Setup will continue.', mbInformation, MB_OK);
end;

// An all-users install goes to Program Files, where the editor cannot save IW8Radiant.ini without administrator rights.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and IsAdminInstallMode then
    if MsgBox('You chose an all-users install. The settings file IW8Radiant.ini (tool paths and the web server login) is kept ' +
              'beside IW8Radiant.exe, and the editor cannot save it there without administrator rights.' + #13#10 + #13#10 +
              'A per-user install avoids this. Continue with the all-users install?', mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    // Settings and caches are kept unless the user says otherwise. Default button is No.
    DeleteUserData := False;
    if not UninstallSilent then
      DeleteUserData := MsgBox('Also delete your settings and caches?' + #13#10 + #13#10 +
                               'Yes: deletes IW8Radiant.ini (tool paths, web server login), the web_tls folder beside the ' +
                               'editor, and %LOCALAPPDATA%\IW8 Radiant Suite (caches, autosaves, crash reports). ' +
                               'This cannot be undone.' + #13#10 + #13#10 +
                               'No: keeps them, so a reinstall picks up where you left off.',
                               mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  end;
  if (CurUninstallStep = usPostUninstall) and DeleteUserData then
  begin
    DeleteFile(ExpandConstant('{app}\IW8Radiant.ini'));
    DelTree(ExpandConstant('{app}\web_tls'), True, True, True);
    DelTree(ExpandConstant('{localappdata}\IW8 Radiant Suite'), True, True, True);
    RemoveDir(ExpandConstant('{app}'));
  end;
end;
