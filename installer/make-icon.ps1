# Packs assets/icon/icon-*.png into installer/InkwyrdAudio.ico, the icon
# the installer itself carries (Inno Setup's SetupIconFile). The PNGs come
# from the test harness:
#
#   $env:INKWYRD_ICONRENDER = 'assets\icon'; AudioEngineTest.exe
#
# The exe's own icon doesn't need this - JUCE builds that from
# assets/icon/icon-256.png (ICON_BIG in src/app/CMakeLists.txt).
#
# Every entry is stored as PNG, which Windows has read inside .ico files
# since Vista; no tool beyond PowerShell is needed.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = foreach ($size in $sizes) {
    $path = Join-Path $root "assets\icon\icon-$size.png"
    [pscustomobject]@{ Size = $size; Bytes = [IO.File]::ReadAllBytes($path) }
}

$stream = New-Object IO.MemoryStream
$writer = New-Object IO.BinaryWriter $stream

# ICONDIR: reserved, type 1 = icon, image count.
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$images.Count)

# ICONDIRENTRY per image, then the image data after all of them.
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $dimension = if ($image.Size -ge 256) { 0 } else { $image.Size }  # 0 means 256
    $writer.Write([byte]$dimension)          # width
    $writer.Write([byte]$dimension)          # height
    $writer.Write([byte]0)                   # palette size
    $writer.Write([byte]0)                   # reserved
    $writer.Write([uint16]1)                 # colour planes
    $writer.Write([uint16]32)                # bits per pixel
    $writer.Write([uint32]$image.Bytes.Length)
    $writer.Write([uint32]$offset)
    $offset += $image.Bytes.Length
}

foreach ($image in $images) { $writer.Write($image.Bytes) }
$writer.Flush()

$out = Join-Path $PSScriptRoot 'InkwyrdAudio.ico'
[IO.File]::WriteAllBytes($out, $stream.ToArray())
"Wrote $out ($($stream.Length) bytes, $($images.Count) sizes)"
