; Inno Setup script for Inkwyrd Audio.
;
; Packages the current build of InkwyrdAudioApp (a console app for now -
; see README.md and CLAUDE.md for status) plus its runtime DLLs. Does
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
#define MyAppExeName "InkwyrdAudioApp.exe"
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
; Unchecked by default and clearly labelled - InkwyrdAudioApp is
; currently a console app that needs PLAYLIST_FOLDER (and optionally
; DISCORD_BOT_TOKEN/DISCORD_GUILD_ID/DISCORD_CHANNEL_ID/SOUNDBOARD_FOLDER)
; set first; launching it blind here would just print that requirement
; and exit. See README.txt.
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName} now (needs environment variables set first - see README.txt)"; Flags: nowait postinstall skipifsilent unchecked
