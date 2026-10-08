param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$Output
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DesktopWindowCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect bounds);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after,
        int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr window, int index);
}
'@
$window = (Get-Process -Id $ProcessId).MainWindowHandle
if ($window -eq [IntPtr]::Zero) { throw 'Desktop client has no capture window' }
[void][DesktopWindowCapture]::ShowWindow($window, 9)
[void][DesktopWindowCapture]::SetForegroundWindow($window)
# Foreground requests can be refused while another app owns keyboard focus.
# Raise just this evidence window for the capture, preserving its prior setting.
$captureWasTopmost = ([DesktopWindowCapture]::GetWindowLong($window, -20) -band 8) -ne 0
[void][DesktopWindowCapture]::SetWindowPos($window, [IntPtr]::new(-1), 0, 0, 0, 0, 0x53)
try {
Start-Sleep -Milliseconds 180
$bounds = New-Object DesktopWindowCapture+Rect
if (-not [DesktopWindowCapture]::GetWindowRect($window, [ref]$bounds)) {
    throw 'Cannot read desktop client window bounds'
}
$width = $bounds.Right - $bounds.Left
$height = $bounds.Bottom - $bounds.Top
if ($width -lt 100 -or $height -lt 100 -or $width -gt 4096 -or $height -gt 4096) {
    throw 'Desktop client window bounds invalid'
}
$bitmap = New-Object System.Drawing.Bitmap($width, $height)
try {
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0,
            (New-Object System.Drawing.Size($width, $height)))
    } finally { $graphics.Dispose() }
    $bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
} finally { $bitmap.Dispose() }
} finally {
    if (-not $captureWasTopmost) {
        [void][DesktopWindowCapture]::SetWindowPos($window, [IntPtr]::new(-2), 0, 0, 0, 0, 0x13)
    }
}
