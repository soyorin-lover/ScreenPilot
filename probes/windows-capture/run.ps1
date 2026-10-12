param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$VisualStudioRoot,
    [ValidateRange(3, 30)][int]$Seconds = 8,
    [string]$OutputDirectory,
    [switch]$InvalidWindow
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDirectory = Join-Path $projectRoot 'build\probes\windows-capture'
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot ('probe-results\capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
if (!$VisualStudioRoot) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$VisualStudioRoot) { throw 'MSVC installation not found. Pass -VisualStudioRoot.' }
}
$devShell = Join-Path $VisualStudioRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShell
Enter-VsDevShell -VsInstallPath $VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$qtCmake = Join-Path $QtRoot 'bin\qt-cmake.bat'
if (!(Test-Path -LiteralPath $qtCmake)) { throw 'Qt 6 development kit not found.' }

& $qtCmake -S $PSScriptRoot -B $buildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $buildDirectory --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

# Pin the experiment backend and plugin directory; do not use Anaconda's Qt 5.
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
$env:QT_MEDIA_BACKEND = 'ffmpeg'
$probeArguments = @('--output-dir', $OutputDirectory, '--seconds', "$Seconds")
if ($InvalidWindow) { $probeArguments += '--invalid-window' }
& (Join-Path $buildDirectory 'screenpilot_window_capture_probe.exe') @probeArguments
$probeExitCode = $LASTEXITCODE
Write-Host "Probe evidence: $OutputDirectory"
Write-Host "Probe exit code: $probeExitCode"
exit $probeExitCode
