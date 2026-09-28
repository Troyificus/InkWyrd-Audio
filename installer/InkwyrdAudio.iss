; Inno Setup script for Inkwyrd Audio.
;
; Packages the current build of InkwyrdAudioApp (a GUI app - see
; README.md and CLAUDE.md for status) plus its runtime DLLs. Does
; NOT install the Stream Deck plugin, which has its own separate install
; flow via `@elgato/cli` and requires Elgato's own Stream Deck software
; to already be present - see streamdeck-plugin/README.md.
;
; Build with: iscc installer\InkwyrdAudio.iss
;
; Requires a Release build already done:
;   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
;   cmake --build build --config Release --target InkwyrdAudioApp
;
; Spelled out here rather than pointing at README.md, which is written
; for people installing the app rather than building it and has no build
; section for this to reference.

#define MyAppName "Inkwyrd Audio"
; The full version string - bump this at the top of EVERY release, not
; just the GitHub release tag/filename. Feeds AppVersion below (so
; Windows' "Installed apps" list actually shows which build is
; installed - it used to just say "0.1.0" for every beta,
; indistinguishable) AND OutputBaseFilename further down, so there's one
; place to update per release rather than two.
;
; NUMBERING, as of 0.1.1-beta. Plain MAJOR.MINOR.PATCH, replacing the
; old "0.1.0-beta.NN" counter that never moved off 0.1.0:
;
;   MAJOR - stays 0 until the full, finished release. Then 1.0.0.
;   MINOR - a beta iteration: new features, anything worth calling a
;           new version of the beta.  0.1.x -> 0.2.0-beta
;   PATCH - a hotfix on the beta that's out.  0.2.0 -> 0.2.1-beta
;
; The "-beta" qualifier stays on until 1.0.0. UpdateCheck.h's comparison
; handles the changeover - 0.1.1-beta really does read as newer than
; 0.1.0-beta.33, and there are checks for exactly that, because getting
; it wrong means nobody on an old build is ever told about an update
; again.
#define MyAppVersion "0.1.1-beta"
#define MyAppPublisher "Troy"
#define MyAppURL "https://github.com/Troyificus/InkWyrd-Audio"
; Windows' numeric version fields hold at most four numbers and can't
; carry a "-beta" qualifier, so they get the numeric part on its own
; while the full string goes in the text fields alongside it.
;
; DERIVED from MyAppVersion rather than typed again: it used to be a
; second literal, and it sat at "0.1.0" through thirty-odd releases
; while the real version moved on. Now there is still exactly one number
; to bump per release.
#define MyAppBaseVersion (Pos("-", MyAppVersion) > 0 ? Copy(MyAppVersion, 1, Pos("-", MyAppVersion) - 1) : MyAppVersion)
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
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
AppCopyright=Copyright (c) 2026 {#MyAppPublisher}
; The installer's own version resource and icon. Without these the setup
; exe said nothing about who made it - one more thing for antivirus
; heuristics to hold against an unsigned download, and the installer is
; what they were flagging (VirusTotal on beta.18: installer 3/71, all
; generic machine-learning/heuristic labels; the program inside it 1/64,
; Microsoft's ML only - and 0/70 for the build before these fields were
; added, so that verdict flips between builds of identical code).
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright=Copyright (c) 2026 {#MyAppPublisher}
VersionInfoDescription={#MyAppName} Setup
VersionInfoProductName={#MyAppName}
VersionInfoVersion={#MyAppBaseVersion}
VersionInfoTextVersion={#MyAppVersion}
VersionInfoProductVersion={#MyAppBaseVersion}
VersionInfoProductTextVersion={#MyAppVersion}
SetupIconFile=InkwyrdAudio.ico
; Per-user install, no admin/UAC needed - simpler for the actual target
; user (a DM setting this up for a game night, not an IT admin) and
; matches how e.g. VS Code and Discord itself install by default.
PrivilegesRequired=lowest
; Inkwyrd is source-available but proprietary (see LICENSE at the repo
; root), so setup shows the terms and asks for agreement rather than
; just dropping a text file in the install folder.
LicenseFile=..\LICENSE
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
; Every DLL beside the exe: the vcpkg ones (opus, sodium, taglib, zlib),
; libdave, AND the Visual C++ runtime (msvcp140/vcruntime140/
; vcruntime140_1), which the build copies there - see
; inkwyrd_copy_msvc_runtime in the root CMakeLists.txt. Shipping the
; runtime is what lets this install and run on a machine that has never
; had the VC++ redistributable on it.
Source: "{#ReleaseDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
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
