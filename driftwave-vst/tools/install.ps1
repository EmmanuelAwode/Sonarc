# Installs (or removes) the Driftwave FX VST3 plugin for every DAW on this PC.
#
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1             # install / update
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Uninstall
#
# Copies "Driftwave FX.vst3" to the standard VST3 folder, C:\Program Files\Common Files\VST3,
# which FL Studio and other DAWs scan. That folder needs administrator rights, so the
# script asks for them once (the usual Windows prompt). Presets and saved colours live in
# Documents\Driftwave FX and are never touched.
param(
    [string]$Plugin = "",
    [string]$Destination = "",   # another VST3 folder (no admin prompt is raised for a custom one)
    [switch]$Uninstall,
    [switch]$Elevated            # set by the script itself when it restarts as administrator
)
$ErrorActionPreference = "Stop"
$bundle = "Driftwave FX.vst3"

function Stop-WithMessage([string]$message) {
    Write-Host ""
    Write-Host $message -ForegroundColor Yellow
    exit 1
}

# From the downloaded zip the plugin sits next to this script; in a source checkout, use the local build.
if (-not $Plugin) {
    $beside = Join-Path $PSScriptRoot $bundle
    $buildDir = if ($env:DRIFTWAVE_BUILD_DIR) { $env:DRIFTWAVE_BUILD_DIR } else { Join-Path (Split-Path $PSScriptRoot -Parent) "build" }
    $Plugin = if (Test-Path $beside) { $beside } else { Join-Path $buildDir "DriftwaveFX_artefacts\Release\VST3\$bundle" }
}

$standard = -not $Destination
if ($standard) { $Destination = Join-Path $env:CommonProgramFiles "VST3" }
$target = Join-Path $Destination $bundle

# The standard folder belongs to Windows: start again as administrator if we are not one.
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if ($standard -and -not $isAdmin) {
    if ($Elevated) { Stop-WithMessage "Could not get administrator rights." }
    if (-not $Uninstall -and -not (Test-Path $Plugin)) { Stop-WithMessage "The plugin was not found at $Plugin." }
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Plugin", "`"$Plugin`"", "-Elevated")
    if ($Uninstall) { $arguments += "-Uninstall" }
    try {
        $process = Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    } catch {
        Stop-WithMessage "Nothing was changed: the administrator prompt was declined."
    }
    if ($process.ExitCode -ne 0) { Stop-WithMessage "Nothing was installed. See the message in the other window." }
    if ($Uninstall) { "Driftwave FX was removed." } else { "Driftwave FX is installed. Open your DAW and rescan plugins if it does not appear." }
    exit 0
}

try {
    if ($Uninstall) {
        if (Test-Path $target) {
            Remove-Item $target -Recurse -Force
            "Removed $target"
        } else {
            "Driftwave FX is not installed in $Destination."
        }
        "Your presets and colours in Documents\Driftwave FX were kept."
    } else {
        if (-not (Test-Path $Plugin)) { throw "The plugin was not found at $Plugin." }
        New-Item -ItemType Directory -Force $Destination | Out-Null
        if (Test-Path $target) { Remove-Item $target -Recurse -Force } # an update replaces the old copy whole
        Copy-Item $Plugin $target -Recurse -Force
        # Files that came from a downloaded zip carry a "from the internet" mark that can stop a DAW loading them.
        Get-ChildItem $target -Recurse -File | Unblock-File
        "Installed $target"
        "In FL Studio: Options > Manage plugins > Find installed plugins, then look for Driftwave FX under Effects."
    }
} catch {
    Write-Host ""
    Write-Host $_.Exception.Message -ForegroundColor Yellow
    Write-Host "If a DAW is open, close it and try again (it keeps the plugin file in use)." -ForegroundColor Yellow
    if ($Elevated) { Read-Host "Press Enter to close" | Out-Null }
    exit 1
}
