# Run this WHILE the Inkwyrd window is showing the pale/ghosted bug.
#
# It captures the same window two different ways, and the difference
# between them is the whole diagnosis:
#
#   *-screen.png   what the COMPOSITOR is putting on the monitor
#   *-printwindow.png  what the APP itself draws, asked for directly
#                       via PrintWindow (bypasses the compositor)
#
# If printwindow looks CORRECT and screen looks wrong, the app is
# painting fine and something between it and the display is showing
# stale pixels - a driver/compositor problem, not ours.
#
# If BOTH look wrong, the app really is painting that, and it's ours.
#
# Also dumps window styles, because WS_EX_LAYERED turning up on a window
# that never asked for it would explain a see-through window outright.

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Cap {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int idx);
  [DllImport("user32.dll")] public static extern bool GetLayeredWindowAttributes(IntPtr h, out uint key, out byte alpha, out uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$outDir = Join-Path $env:TEMP "inkwyrd-window-bug"
New-Item -ItemType Directory -Force $outDir | Out-Null

$procs = Get-Process | Where-Object { $_.ProcessName -like '*Inkwyrd*' }
if (-not $procs) { Write-Host "Inkwyrd Audio isn't running."; exit 1 }

foreach ($p in $procs) {
    Write-Host "PID $($p.Id)  Responding=$($p.Responding)"
}

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Window)

foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    $proc = Get-Process -Id $w.Current.ProcessId -ErrorAction SilentlyContinue
    if (-not $proc -or $proc.ProcessName -notlike '*Inkwyrd*') { continue }

    $h = [IntPtr][int64]$w.Current.NativeWindowHandle
    $name = ($w.Current.Name -replace '[^A-Za-z0-9]', '_')

    $r = New-Object Cap+RECT
    [Cap]::GetWindowRect($h, [ref]$r) | Out-Null
    $width = $r.Right - $r.Left
    $height = $r.Bottom - $r.Top
    if ($width -le 0 -or $height -le 0) { continue }

    # GWL_EXSTYLE = -20. 0x00080000 = WS_EX_LAYERED.
    $ex = [Cap]::GetWindowLong($h, -20)
    $layered = ($ex -band 0x00080000) -ne 0
    $alphaText = ""
    if ($layered) {
        $key = 0; $alpha = 0; $flags = 0
        if ([Cap]::GetLayeredWindowAttributes($h, [ref]$key, [ref]$alpha, [ref]$flags)) {
            $alphaText = "  alpha=$alpha flags=0x$('{0:X}' -f $flags)"
        }
    }
    Write-Host ("{0,-16} {1}x{2} at {3},{4}  exstyle=0x{5:X8} layered={6}{7}" -f `
        $w.Current.Name, $width, $height, $r.Left, $r.Top, $ex, $layered, $alphaText)

    # 1. What's actually on the monitor.
    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save((Join-Path $outDir "$name-screen.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()

    # 2. What the app draws when asked directly. Flag 2 =
    # PW_RENDERFULLCONTENT, needed for windows that render via DWM.
    $bmp2 = New-Object System.Drawing.Bitmap $width, $height
    $g2 = [System.Drawing.Graphics]::FromImage($bmp2)
    $hdc = $g2.GetHdc()
    [Cap]::PrintWindow($h, $hdc, 2) | Out-Null
    $g2.ReleaseHdc($hdc)
    $g2.Dispose()
    $bmp2.Save((Join-Path $outDir "$name-printwindow.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp2.Dispose()
}

Write-Host ""
Write-Host "Saved to: $outDir"
