# Installs the built Sonarc for the current user and adds Start menu and desktop shortcuts.
#
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1            # install / update
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1 -NoDesktop # Start menu only
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Uninstall
#
# Copies Sonarc.exe to %LOCALAPPDATA%\Programs\Sonarc (no admin rights needed), so
# rebuilding doesn't touch the installed copy. Run it again after a rebuild to update.
# Settings stay in %APPDATA%\Sonarc either way. To put Sonarc on the taskbar, open
# it from Start, right-click its taskbar icon and choose "Pin to taskbar" (Windows
# doesn't let programs pin themselves).
param(
    [string]$Exe = "",
    [switch]$NoDesktop,
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"

# From the downloaded release zip, Sonarc.exe sits next to this script;
# in a source checkout, use the local build.
if (-not $Exe) {
    $beside = Join-Path $PSScriptRoot "Sonarc.exe"
    $Exe = if (Test-Path $beside) { $beside } else { "$env:LOCALAPPDATA\SonarcBuild\src\Sonarc_artefacts\Release\Sonarc.exe" }
}

$installDir = Join-Path $env:LOCALAPPDATA "Programs\Sonarc"
$installedExe = Join-Path $installDir "Sonarc.exe"
$startMenuLink = Join-Path ([Environment]::GetFolderPath("Programs")) "Sonarc.lnk"
$desktopLink = Join-Path ([Environment]::GetFolderPath("Desktop")) "Sonarc.lnk"

# The installed copy can't be replaced while it runs: close it politely first.
$running = Get-Process Sonarc -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $installedExe }
foreach ($p in $running) {
    $p.CloseMainWindow() | Out-Null
    if (-not $p.WaitForExit(5000)) { throw "Sonarc is still running from $installDir; close it and try again." }
}

if ($Uninstall) {
    foreach ($path in @($startMenuLink, $desktopLink, $installDir)) {
        if (Test-Path $path) { Remove-Item $path -Recurse -Force; "Removed $path" }
    }
    "Sonarc uninstalled (settings in $env:APPDATA\Sonarc were kept)."
    return
}

if (-not (Test-Path $Exe)) { throw "No build found at $Exe. Build Sonarc first." }

New-Item -ItemType Directory -Force $installDir | Out-Null
Copy-Item $Exe $installedExe -Force
"Installed $installedExe"

$shell = New-Object -ComObject WScript.Shell
$links = @($startMenuLink)
if (-not $NoDesktop) { $links += $desktopLink }
foreach ($path in $links) {
    $link = $shell.CreateShortcut($path)
    $link.TargetPath = $installedExe
    $link.WorkingDirectory = $installDir
    $link.IconLocation = "$installedExe,0"
    $link.Description = "Sonarc - real-time audio visualiser"
    $link.Save()
    "Shortcut: $path"
}
"Done. Open Sonarc from Start (or the desktop), then right-click its taskbar icon > Pin to taskbar."
