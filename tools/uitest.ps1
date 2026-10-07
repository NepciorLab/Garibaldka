# UI test helper for Garibaldi.exe. Sends mouse/keyboard messages straight to the game window
# (nothing is injected into the desktop, other windows are never touched) and saves screenshots
# of that window only (PrintWindow).
#   powershell -ExecutionPolicy Bypass -File tools/uitest.ps1 -Actions "click 538 750; wait 900; shot a.png"
# Actions: chars TEXT | char CODE | click X Y | drag X0 Y0 X1 Y1 | dbl X Y | key VK_DEC | wait MS | shot NAME   (client coordinates)
param([string]$Actions,[int]$Which=1,[int]$ProcId=0)   # -ProcId: drive exactly this process (never touch another running copy of the game)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class U{
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 public struct RECT{public int L,T,R,B;}
 public static IntPtr LP(int x,int y){return (IntPtr)((y<<16)|(x&0xFFFF));}
}
"@
if($ProcId -gt 0){ $p=Get-Process -Id $ProcId -ErrorAction Stop } else { $p=(Get-Process Garibaldi -ErrorAction Stop | Sort-Object StartTime)[$Which-1] }
$h=$p.MainWindowHandle
function Msg($m,$w,$x,$y){[U]::PostMessage($h,$m,[IntPtr]$w,[U]::LP($x,$y))|Out-Null}
foreach($a in $Actions.Split(';')){
 $t=$a.Trim().Split(' ')
 switch($t[0]){
  'click' {Msg 0x200 0 $t[1] $t[2]; Start-Sleep -Milliseconds 40; Msg 0x201 1 $t[1] $t[2]; Start-Sleep -Milliseconds 60; Msg 0x202 0 $t[1] $t[2]}
  'dbl'   {Msg 0x201 1 $t[1] $t[2]; Msg 0x202 0 $t[1] $t[2]; Start-Sleep -Milliseconds 30; Msg 0x203 1 $t[1] $t[2]; Msg 0x202 0 $t[1] $t[2]}
  'drag'  {$x0=[int]$t[1];$y0=[int]$t[2];$x1=[int]$t[3];$y1=[int]$t[4]
           Msg 0x200 0 $x0 $y0; Start-Sleep -Milliseconds 40; Msg 0x201 1 $x0 $y0
           1..14|%{ Msg 0x200 1 ([int]($x0+($x1-$x0)*$_/14)) ([int]($y0+($y1-$y0)*$_/14)); Start-Sleep -Milliseconds 25 }
           Start-Sleep -Milliseconds 80; Msg 0x202 0 $x1 $y1}
  'key'   {[U]::PostMessage($h,0x100,[IntPtr][int]$t[1],[IntPtr]0)|Out-Null}
  'chars' {foreach($ch in $a.Trim().Substring(6).ToCharArray()){ [U]::PostMessage($h,0x102,[IntPtr][int]$ch,[IntPtr]0)|Out-Null; Start-Sleep -Milliseconds 15 }}
  'char'  {[U]::PostMessage($h,0x102,[IntPtr][int]$t[1],[IntPtr]0)|Out-Null}
  'wait'  {Start-Sleep -Milliseconds ([int]$t[1])}
  'shot'  {$r=New-Object U+RECT;[U]::GetWindowRect($h,[ref]$r)|Out-Null
           $b=New-Object System.Drawing.Bitmap ($r.R-$r.L),($r.B-$r.T); $g=[System.Drawing.Graphics]::FromImage($b)
           $dc=$g.GetHdc(); [U]::PrintWindow($h,$dc,2)|Out-Null; $g.ReleaseHdc($dc)
           $b.Save("G:\Claude\Garibaldi\build\$($t[1])"); $g.Dispose();$b.Dispose()}
 }
}
"done"
