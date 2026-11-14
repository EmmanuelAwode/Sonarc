# Builds the downloadable release zip from the local Release build:
#
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1
#
# Writes dist\DriftwaveFX-<version>-windows-x64.zip containing the plugin, the installer
# (double-click "Install Driftwave FX.cmd"), the licence and a short read-me.
# The plugin is built with the static runtime, so the zip needs nothing else installed.
param([string]$Plugin = "")
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$bundle = "Driftwave FX.vst3"
if (-not $Plugin) {
    $buildDir = if ($env:DRIFTWAVE_BUILD_DIR) { $env:DRIFTWAVE_BUILD_DIR } else { Join-Path $root "build" }
    $Plugin = Join-Path $buildDir "DriftwaveFX_artefacts\Release\VST3\$bundle"
}
if (-not (Test-Path $Plugin)) { throw "No build found at $Plugin. Run build.cmd first." }

$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern "project\(DriftwaveFX VERSION ([0-9.]+)").Matches[0].Groups[1].Value
$name = "DriftwaveFX-$version-windows-x64"
$stage = Join-Path $root "dist\$name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $Plugin (Join-Path $stage $bundle) -Recurse
Copy-Item (Join-Path $PSScriptRoot "install.ps1") $stage
Copy-Item (Join-Path (Split-Path $root -Parent) "LICENSE") (Join-Path $stage "LICENSE.txt")

Set-Content -Encoding ascii (Join-Path $stage "Install Driftwave FX.cmd") @'
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
pause
'@

Set-Content -Encoding ascii (Join-Path $stage "Uninstall Driftwave FX.cmd") @'
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall
pause
'@

Set-Content -Encoding utf8 (Join-Path $stage "READ ME.txt") @"
Driftwave FX $version - slowed + reverb effect plugin (VST3) for Windows 10 / 11, 64-bit
Works in FL Studio and any other DAW that loads VST3 effects.

Install
  1. Unzip this folder somewhere first (right-click the zip > Extract All).
  2. Close your DAW.
  3. Double-click "Install Driftwave FX.cmd" and say Yes to the Windows prompt.
     The plugin is copied to C:\Program Files\Common Files\VST3.
  4. Open your DAW. In FL Studio: Options > Manage plugins > Find installed plugins,
     then add Driftwave FX from Effects (it is made for the master channel).

Windows may show "Windows protected your PC" because the installer is not
code-signed: click "More info" > "Run anyway".

By hand instead
  Copy the "Driftwave FX.vst3" folder into C:\Program Files\Common Files\VST3.

Update
  Install the new version the same way; it replaces the old one.
  Your presets and colours are kept.

Uninstall
  Double-click "Uninstall Driftwave FX.cmd". Your presets and colours stay in
  Documents\Driftwave FX; delete that folder too if you want them gone.

Nothing else needs installing. The plugin does not use the internet.

Licence: GNU AGPL v3 (see LICENSE.txt). Source code and updates: see the page
this was downloaded from.
"@

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
"Wrote $zip ({0:N1} MB)" -f ((Get-Item $zip).Length / 1MB)
