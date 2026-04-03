; =============================================================================
; TStar Installer Script for Inno Setup
;
; Builds a Windows installer package for TStar.
; Download Inno Setup from https://jrsoftware.org/isinfo.php
;
; Version is passed from the command line via /DMyAppVersion="x.x.x".
; Defaults to 1.0.0 if not specified.
; =============================================================================

#define MyAppName      "TStar"

#ifndef MyAppVersion
    #define MyAppVersion "1.0.0"
#endif

#define MyAppPublisher "Fabio Tempera"
#define MyAppURL       "https://github.com/Ft2801/TStar"
#define MyAppExeName   "TStar.exe"

; =============================================================================
; [Setup] - General installer configuration
; =============================================================================

[Setup]
AppId={{B5F8E3A4-2D91-4C67-9A5E-7F2B3C8D1E9A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
PrivilegesRequired=admin
LicenseFile=..\LICENSE
SetupIconFile=images\Logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=..\installer_output
OutputBaseFilename=TStar_Setup_{#MyAppVersion}
Compression=lzma2/normal
SolidCompression=yes
InfoBeforeFile=..\changelog.txt

; Modern visual style
WizardStyle=modern
WizardImageFile=images\WizardImage.bmp
WizardSmallImageFile=images\WizardSmallImage.bmp

; 64-bit only
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

; Embedded version metadata
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription=TStar Astrophotography Application Setup
VersionInfoCopyright=Copyright (C) 2026 Fabio Tempera
VersionInfoProductName=TStar
VersionInfoProductVersion={#MyAppVersion}

; =============================================================================
; [Languages] - Supported installer languages
; =============================================================================

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

; =============================================================================
; [CustomMessages] - Localized custom messages
; =============================================================================

[CustomMessages]
english.UninstallOldVersion=An older version of TStar was detected. It will be uninstalled before proceeding.
italian.UninstallOldVersion=È stata rilevata una versione precedente di TStar. Verrà disinstallata prima di procedere.

; =============================================================================
; [Tasks] - Optional installation tasks
; =============================================================================

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

; =============================================================================
; [Files] - Files to install
; =============================================================================

[Files]
; Main application files (recursive, excluding user scripts)
Source: "..\dist\TStar\*"; \
    DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "scripts\*"

; User scripts (preserve existing files on upgrade)
Source: "..\dist\TStar\scripts\*"; \
    DestDir: "{app}\scripts"; \
    Flags: onlyifdoesntexist recursesubdirs createallsubdirs

; =============================================================================
; [Icons] - Start menu and desktop shortcuts
; =============================================================================

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

; =============================================================================
; [Run] - Post-installation actions
; =============================================================================

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; \
    Flags: nowait postinstall skipifsilent

; =============================================================================
; [Code] - Pascal Script for automatic uninstall of previous versions
; =============================================================================

[Code]

{ Retrieve the uninstall command string from the registry, if present. }
function GetUninstallString(): String;
var
    sUnInstPath: String;
    sUnInstallString: String;
begin
    sUnInstPath := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{B5F8E3A4-2D91-4C67-9A5E-7F2B3C8D1E9A}_is1';
    sUnInstallString := '';

    if not RegQueryStringValue(HKLM64, sUnInstPath, 'UninstallString', sUnInstallString) then
        if not RegQueryStringValue(HKLM32, sUnInstPath, 'UninstallString', sUnInstallString) then
            RegQueryStringValue(HKCU, sUnInstPath, 'UninstallString', sUnInstallString);

    Result := sUnInstallString;
end;

{ Automatically uninstall any previous version before proceeding. }
function InitializeSetup(): Boolean;
var
    sUninstallString: String;
    iResultCode: Integer;
begin
    sUninstallString := GetUninstallString();
    if sUninstallString <> '' then
    begin
        MsgBox(CustomMessage('UninstallOldVersion'), mbInformation, MB_OK);
        sUninstallString := RemoveQuotes(sUninstallString);
        Exec(sUninstallString, '/SILENT /SUPPRESSMSGBOXES', '', SW_SHOWNORMAL,
             ewWaitUntilTerminated, iResultCode);
    end;
    Result := True;
end;