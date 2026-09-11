# Builds the portable ZIP that ships alongside the installer:
#   installer\Output\InkwyrdAudio-Portable-<version>.zip
#
# Exactly the files the installer puts in place (see [Files] in
# InkwyrdAudio.iss), in an "Inkwyrd Audio" folder - unzip and run.
#
# Why this exists: antivirus heuristics flagged the unsigned INSTALLER
# (VirusTotal 4/71, every hit a generic machine-learning or "behaves like
# a dropper" label - which is what any installer that unpacks files looks
# like), while the program inside it scanned 0/70. The ZIP gets people
# the program without the wrapper that trips them.
#
# Run after the Release build, alongside `iscc installer\InkwyrdAudio.iss`.
# The version comes from MyAppVersion in the .iss, so there's still only
# one place to bump per release.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$iss = Get-Content (Join-Path $PSScriptRoot 'InkwyrdAudio.iss') -Raw
if ($iss -notmatch '#define MyAppVersion "([^"]+)"') { throw 'MyAppVersion not found in InkwyrdAudio.iss' }
$version = $Matches[1]

$release = Join-Path $root 'build\src\app\InkwyrdAudioApp_artefacts\Release'
$outDir = Join-Path $PSScriptRoot 'Output'
$staging = Join-Path $outDir 'portable-staging'
$appDir = Join-Path $staging 'Inkwyrd Audio'
$zip = Join-Path $outDir "InkwyrdAudio-Portable-$version.zip"

if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force $appDir | Out-Null

# The named exe only - the Release folder can also hold stale binaries
# from older target names, which must not ship.
Copy-Item (Join-Path $release 'Inkwyrd Audio.exe') $appDir
Copy-Item (Join-Path $release '*.dll') $appDir
Copy-Item (Join-Path $root 'README.md') (Join-Path $appDir 'README.txt')
Copy-Item (Join-Path $root 'docs\THIRD_PARTY_LICENSES.md') (Join-Path $appDir 'THIRD_PARTY_LICENSES.txt')
Copy-Item (Join-Path $PSScriptRoot 'ThirdPartyNotices.txt') $appDir

if (Test-Path $zip) { Remove-Item $zip -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($staging, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
Remove-Item $staging -Recurse -Force

Get-Item $zip | Select-Object Name, Length
Add-Type -AssemblyName System.IO.Compression
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try { $archive.Entries | ForEach-Object { '  ' + $_.FullName } } finally { $archive.Dispose() }
