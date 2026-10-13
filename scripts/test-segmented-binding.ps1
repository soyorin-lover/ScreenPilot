param([Parameter(Mandatory = $true)][string]$QtRoot)
$ErrorActionPreference = 'Stop'
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('screenpilot-segmented-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null
try {
    $appDirectory = Join-Path $PSScriptRoot '../app'
    foreach ($name in @('StudioSegmented.qml', 'StudioTheme.qml', 'StudioIcon.qml')) {
        Copy-Item -LiteralPath (Join-Path $appDirectory $name) -Destination $testDirectory
    }
    Set-Content -LiteralPath (Join-Path $testDirectory 'qmldir') -Value 'singleton StudioTheme 1.0 StudioTheme.qml'
    @'
import QtQuick
import QtTest
import "."
Item {
    width: 300; height: 100
    property int backendIndex: 0
    StudioSegmented {
        id: choice
        width: 240
        model: ["First", "Second"]
        currentIndex: backendIndex
        onActivated: index => backendIndex = index
    }
    TestCase {
        name: "SegmentedBinding"
        when: windowShown
        function test_externalUpdateAfterClick() {
            mouseClick(choice, 180, 18)
            compare(backendIndex, 1)
            compare(choice.currentIndex, 1)
            backendIndex = 0 // Undo, project load, or preset application.
            compare(choice.currentIndex, 0)
            mouseClick(choice, 180, 18)
            compare(backendIndex, 1)
            compare(choice.currentIndex, 1)
        }
    }
}
'@ | Set-Content -LiteralPath (Join-Path $testDirectory 'tst_segmented.qml')
    & (Join-Path $QtRoot 'bin/qmltestrunner.exe') -platform offscreen -input $testDirectory
    if ($LASTEXITCODE -ne 0) { throw "Segmented binding regression failed ($LASTEXITCODE)." }
} finally {
    # The target is the unique temporary child created above, never a supplied path.
    $resolvedTestDirectory = [IO.Path]::GetFullPath($testDirectory)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (!$resolvedTestDirectory.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Temporary test directory escaped its parent.'
    }
    Remove-Item -LiteralPath $resolvedTestDirectory -Recurse -Force
}
