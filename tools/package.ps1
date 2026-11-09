# Builds the downloadable release zip from the local Release build:
#
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1
#
# Writes dist\Sonarc-<version>-windows-x64.zip containing Sonarc.exe, the
# installer (double-click "Install Sonarc.cmd"), and a short read-me.
param([string]$Exe = "$env:LOCALAPPDATA\SonarcBuild\src\Sonarc_artefacts\Release\Sonarc.exe")
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path $Exe)) { throw "No build found at $Exe. Build Sonarc (Release) first." }

$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern "project\(Sonarc VERSION ([0-9.]+)").Matches[0].Groups[1].Value
$name = "Sonarc-$version-windows-x64"
$stage = Join-Path $root "dist\$name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $Exe (Join-Path $stage "Sonarc.exe")
Copy-Item (Join-Path $PSScriptRoot "install.ps1") $stage
Copy-Item (Join-Path $root "LICENSE") $stage

Set-Content -Encoding ascii (Join-Path $stage "Install Sonarc.cmd") @'
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
pause
'@

Set-Content -Encoding ascii (Join-Path $stage "Uninstall Sonarc.cmd") @'
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall
pause
'@

Set-Content -Encoding utf8 (Join-Path $stage "READ ME.txt") @"
Sonarc $version - real-time audio visualiser for Windows 10 (2004+) / 11

Install:   double-click "Install Sonarc.cmd". Sonarc is added to the Start menu
           and the desktop (installed to %LOCALAPPDATA%\Programs\Sonarc, no admin needed).
Taskbar:   open Sonarc from Start, right-click its taskbar icon > Pin to taskbar.
Portable:  or just run Sonarc.exe from this folder.
Uninstall: double-click "Uninstall Sonarc.cmd" (your settings are kept).

Windows may show "Windows protected your PC" because the app isn't code-signed:
click "More info" > "Run anyway".

Source, docs and updates: see the GitHub page this was downloaded from.
"@

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
"Wrote $zip"
