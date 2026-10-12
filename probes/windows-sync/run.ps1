param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$VisualStudioRoot,
    [string]$OutputDirectory,
    [switch]$NoInput
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDirectory = Join-Path $projectRoot 'build\probes\windows-sync'
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot ('probe-results\sync-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
if (!$VisualStudioRoot) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$VisualStudioRoot) { throw 'MSVC installation not found. Pass -VisualStudioRoot.' }
}
Import-Module (Join-Path $VisualStudioRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$qtCmake = Join-Path $QtRoot 'bin\qt-cmake.bat'
if (!(Test-Path -LiteralPath $qtCmake)) { throw 'Qt 6 development kit not found.' }
& $qtCmake -S $PSScriptRoot -B $buildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $buildDirectory --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
$probeArguments = @('--output-dir', $OutputDirectory)
if ($NoInput) { $probeArguments += '--no-input' }
& (Join-Path $buildDirectory 'screenpilot_windows_sync_probe.exe') @probeArguments
$probeExitCode = $LASTEXITCODE
Write-Host "Probe evidence: $OutputDirectory"
Write-Host "Probe exit code: $probeExitCode"
exit $probeExitCode
