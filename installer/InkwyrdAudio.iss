; Inno Setup script for Inkwyrd Audio.
;
; Packages the current build of InkwyrdAudioApp (a GUI app - see
; README.md and CLAUDE.md for status) plus its runtime DLLs. Does
; NOT install the Stream Deck plugin, which has its own separate install
; flow via `@elgato/cli` and requires Elgato's own Stream Deck software
; to already be present - see streamdeck-plugin/README.md.
;
; Build with: iscc installer\InkwyrdAudio.iss
; (requires a Release build already done - see the CMake command in
; README.md's "Building the installer" section)

#define MyAppName "Inkwyrd Audio"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Troy"
; The CMake target is named InkwyrdAudioApp, but juce_add_gui_app names
; the actual output binary after PRODUCT_NAME ("Inkwyrd Audio") - unlike
; juce_add_console_app, which used the target name. Confirmed by building
; and listing the real artefact directory, not assumed.
#define MyAppExeName "Inkwyrd Audio.exe"
#define ReleaseDir "..\build\src\app\InkwyrdAudioApp_artefacts\Release"

[Setup]
; Fixed, persistent GUID - identifies this app across versions for
; clean upgrades/uninstalls. Do not regenerate this on future builds.
AppId={{CA652386-31DE-4EC3-8625-D21D268A5831}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
; Per-user install, no admin/UAC needed - simpler for the actual target
; user (a DM setting this up for a game night, not an IT admin) and
; matches how e.g. VS Code and Discord itself install by default.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=Output
; NOTE: temporarily "-v2-" suffixed, not the clean
; "InkwyrdAudio-Setup-{#MyAppVersion}" - two zombie elevated processes from
; an earlier failed admin-install test (PIDs 51104/32332, from before the
; PrivilegesRequired=lowest fix) are still holding the original filename's
; output .exe open and can't be killed from this non-admin session (Access
; is denied). Revert this once those processes are gone - a reboot, the
; user manually ending them in Task Manager, or Windows eventually
; reclaiming them.
OutputBaseFilename=InkwyrdAudio-Setup-v2-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile=ThirdPartyNotices.txt
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#ReleaseDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\THIRD_PARTY_LICENSES.md"; DestDir: "{app}"; DestName: "THIRD_PARTY_LICENSES.txt"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Read Me"; Filename: "{app}\README.txt"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Run]
; InkwyrdAudioApp is now a GUI app that walks the user through setup
; (folder picker, optional Discord bot fields) on first launch - no
; preconditions needed, so this is checked by default.
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName} now"; Flags: nowait postinstall skipifsilent
