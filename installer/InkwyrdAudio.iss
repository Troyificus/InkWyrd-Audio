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
; The full beta-qualified version string - bump this at the top of
; EVERY release, hotfix or feature (e.g. "0.1.0-beta.2.2"), not just
; the GitHub release tag/filename. Feeds AppVersion below (so Windows'
; "Installed apps" list actually shows which beta is installed - it
; used to just say "0.1.0" for every beta, indistinguishable) AND
; OutputBaseFilename further down, so there's one place to update per
; release rather than two.
#define MyAppVersion "0.1.0-beta.15"
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
; Still "-v2-" suffixed, not the clean "InkwyrdAudio-Setup-{#MyAppVersion}" -
; two zombie elevated processes (PIDs 51104/32332, from a UAC-hang test
; predating the PrivilegesRequired=lowest fix) are STILL holding the
; original filename open in a later session, `taskkill /F` on either PID
; fails with Access is denied from this non-admin session - genuinely
; can't be cleared without a real reboot or the user manually ending them
; in Task Manager. Revert once they're actually gone (confirm with
; `tasklist /FI "PID eq 51104"` and `/FI "PID eq 32332"` as TWO SEPARATE
; calls, not one call with both filters - combined filters AND together
; and can falsely report "no tasks" for either PID individually).
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
