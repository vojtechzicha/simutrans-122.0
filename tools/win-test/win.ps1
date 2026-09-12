# Helpers for driving the Simutrans GDI window on Windows from a script.
# Usage:
#   pwsh tools/win-test/win.ps1 shot OUT.png        screenshot the game window, print its client rect
#   pwsh tools/win-test/win.ps1 click X Y [left|right|middle]   click at client coordinates
#   pwsh tools/win-test/win.ps1 move X Y            move the cursor to client coordinates
#   pwsh tools/win-test/win.ps1 keys "text"         send keystrokes (SendKeys syntax, e.g. "{ENTER}")
#   pwsh tools/win-test/win.ps1 wheel X Y DELTA     mouse wheel at client coordinates
param([string]$cmd, [string]$a, [string]$b, [string]$c)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public struct RECT { public int L, T, R, B; }
public struct POINT { public int X, Y; }
public static class W {
  [DllImport("user32.dll")] public static extern IntPtr FindWindowW(string cls, string title);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, int data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
}
"@
function Find-Game {
  $p = Get-Process | Where-Object { $_.MainWindowTitle -like 'Simutrans*' } | Select-Object -First 1
  if (-not $p) { throw "no Simutrans window" }
  return [IntPtr]$p.MainWindowHandle
}
function Client-Origin($h) { $p = New-Object POINT; [W]::ClientToScreen($h, [ref]$p) | Out-Null; return $p }
$h = Find-Game
$o = Client-Origin $h
$r = New-Object RECT; [W]::GetClientRect($h, [ref]$r) | Out-Null
switch ($cmd) {
  'shot' {
    [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 150
    $bmp = New-Object System.Drawing.Bitmap $r.R, $r.B
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($o.X, $o.Y, 0, 0, $bmp.Size); $g.Dispose()
    $bmp.Save($a, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "client origin $($o.X),$($o.Y) size $($r.R)x$($r.B) -> $a"
  }
  'move' { [W]::SetForegroundWindow($h) | Out-Null; [W]::SetCursorPos($o.X + [int]$a, $o.Y + [int]$b) | Out-Null }
  'click' {
    [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 100
    $x = $o.X + [int]$a; $y = $o.Y + [int]$b
    [W]::SetCursorPos($x - 2, $y - 2) | Out-Null; Start-Sleep -Milliseconds 60
    [W]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 60
    switch ($c) {
      'right'  { [W]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [W]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero) }
      'middle' { [W]::mouse_event(0x0020, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [W]::mouse_event(0x0040, 0, 0, 0, [UIntPtr]::Zero) }
      default  { [W]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [W]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero) }
    }
    "clicked $c at $a,$b"
  }
  'wheel' {
    [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 100
    [W]::SetCursorPos($o.X + [int]$a, $o.Y + [int]$b) | Out-Null; Start-Sleep -Milliseconds 60
    [W]::mouse_event(0x0800, 0, 0, [int]$c, [UIntPtr]::Zero)
  }
  'keys' {
    [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 150
    [System.Windows.Forms.SendKeys]::SendWait($a); "sent $a"
  }
  default { "client origin $($o.X),$($o.Y) size $($r.R)x$($r.B)" }
}
