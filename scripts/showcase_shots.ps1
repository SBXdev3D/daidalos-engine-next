# Takes the factory showcase's screenshots from fixed places (ADR-0086), to compare a change with the last set.
#
#   powershell -File scripts/showcase_shots.ps1 [-Out <folder>] [-Only 1-start,5-pumps] [-Bright] [-Extra +name,value]
#
# Starts the sandbox at each place with the flashlight on, waits for the level and its effects to settle,
# and saves what the window shows (PrintWindow) as <folder>/<place>.png. -Bright shows the level 16 times
# brighter without its haze (sandbox.darkness 0), to check where things stand. -Extra adds settings to the
# command line, to compare one. Windows only.
param(
    [string]$Out = "build/showcase",
    [string[]]$Only = @(),
    [switch]$Bright,
    [string[]]$Extra = @(),
    [int]$Wait = 22
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$exe = Get-ChildItem (Join-Path $root "build") -Recurse -Filter sandbox.exe |
    Where-Object { $_.FullName -match "\\Release\\" -and $_.FullName -notmatch "shipping" } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($null -eq $exe) { throw "No Release sandbox.exe under build/: build it first" }
New-Item -ItemType Directory -Force (Join-Path $root $Out) | Out-Null

# Place: "x y z yaw pitch", eye position in meters, angles in degrees (yaw 0 looks along -Z, grows to the left).
$places = [ordered]@{
    "1-start"     = "0 1.6 -7 180 -5"
    "2-hall"      = "0 1.6 -30 0 5"
    "3-west"      = "-12 1.6 -24 30 0"
    "4-offices"   = "40 4.4 -42 0 0"
    "5-pumps"     = "-37 -6.8 -37.5 0 0"
    "6-barrels"   = "-36.8 -6.8 -45.8 90 0"
    "7-yard"      = "4 1.6 -80 0 0"
    "8-workshop"  = "40 1.6 -31 0 -5"
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ShowcaseWindow {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

foreach ($name in $places.Keys) {
    if ($Only.Count -gt 0 -and -not ($Only -contains $name)) { continue }
    $arguments = @("+window.fullscreen", "false", "+window.width", "1280", "+window.height", "720",
                   "+sandbox.menu", "false", "+sandbox.flashlight", "true", "+sandbox.start", ('"' + $places[$name] + '"'))
    if ($Bright) { $arguments += @("+sandbox.darkness", "0") }
    $arguments += $Extra
    $process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -WorkingDirectory $exe.DirectoryName -PassThru
    Start-Sleep -Seconds $Wait
    $process.Refresh()
    $rect = New-Object ShowcaseWindow+RECT
    [ShowcaseWindow]::GetClientRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    $bitmap = New-Object System.Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [ShowcaseWindow]::PrintWindow($process.MainWindowHandle, $hdc, 3) | Out-Null
    $graphics.ReleaseHdc($hdc)
    $file = Join-Path (Join-Path $root $Out) "$name.png"
    $bitmap.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Stop-Process -Id $process.Id -Force
    Write-Output $file
}
