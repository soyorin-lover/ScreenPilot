param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$VisualStudioRoot,
    [switch]$Deploy,
    [string]$DeploymentDirectory,
    [switch]$Launch
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build\desktop'
if (!$VisualStudioRoot) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$VisualStudioRoot) { throw 'Visual Studio not found. Pass -VisualStudioRoot.' }
}
Import-Module (Join-Path $VisualStudioRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
& (Join-Path $QtRoot 'bin\qt-cmake.bat') -S $projectRoot -B $buildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
& cmake --build $buildDirectory --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$executable = Join-Path $buildDirectory 'ScreenPilot.exe'
if ($Deploy) {
    $portableDirectory = if ($DeploymentDirectory) { [System.IO.Path]::GetFullPath($DeploymentDirectory) } else { Join-Path $buildDirectory 'portable-playback' }
    New-Item -ItemType Directory -Force -Path $portableDirectory | Out-Null
    Copy-Item -LiteralPath $executable -Destination $portableDirectory
    Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\DogSC-LICENSE') -Destination (Join-Path $portableDirectory 'DogSC-LICENSE.txt')
    $executable = Join-Path $portableDirectory 'ScreenPilot.exe'
    & (Join-Path $QtRoot 'bin\windeployqt.exe') --release --no-translations --qmldir (Join-Path $projectRoot 'app') $executable
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
}
if ($Launch) {
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    $env:QML_IMPORT_PATH = Join-Path $QtRoot 'qml'
    Start-Process -FilePath $executable -WorkingDirectory (Split-Path -Parent $executable)
}
Write-Output "Application: $executable"
