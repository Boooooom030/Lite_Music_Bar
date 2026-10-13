[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SdkRoot,
    [switch]$UseCachedSdk,
    # Local verification only: reuse an already installed, matching Qt prefix.
    [switch]$ReuseInstalledDependencies
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed (exit $LASTEXITCODE)." }
}
function Assert-Commit([string]$Directory, [string]$Expected) {
    $actual = & git -C $Directory rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $Expected) {
        throw "Unexpected source revision at ${Directory}: $actual; expected $Expected"
    }
}
function Assert-Qt([string]$Prefix) {
    $version = Get-Content -Raw -LiteralPath "$Prefix/include/Qt6/QtCore/qtcoreversion.h"
    $features = Get-Content -Raw -LiteralPath "$Prefix/include/Qt6/QtGui/qtgui-config.h"
    $coreFeatures = Get-Content -Raw -LiteralPath "$Prefix/include/Qt6/QtCore/qconfig.h"
    if ($version -notmatch 'QTCORE_VERSION_STR "6\.11\.2"') { throw 'Qt 6.11.2 is required.' }
    if ($features -notmatch '#define QT_FEATURE_harfbuzz 1') { throw 'Qt Gui must include HarfBuzz.' }
    if ($coreFeatures -notmatch '#define QT_FEATURE_cxx20 1') { throw 'Build Qt with FEATURE_cxx20=ON to match the application coroutine ABI.' }
    foreach ($file in @('lib/Qt6Core.lib', 'lib/Qt6Gui.lib', 'lib/Qt6Widgets.lib', 'share/Qt6/Qt6Config.cmake')) {
        if (-not (Test-Path -LiteralPath "$Prefix/$file" -PathType Leaf)) { throw "Missing static Qt SDK file: $file" }
    }
}

$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$vcpkgCommit = 'e456309491fd875487bf02b8a0dca76c1b1b00cc'
$fluentCommit = '4076a1612e5785757cfe0759436dabe3414e490e'
$triplet = 'x64-windows-static-release'
$qtPrefix = Join-Path $SdkRoot "installed/$triplet"
$fluentPrefix = Join-Path $SdkRoot 'sdk/FluentQt'
Assert-Commit "$SdkRoot/vcpkg" $vcpkgCommit
Assert-Commit "$SdkRoot/Fluent-Qt" $fluentCommit
Get-Command cl, cmake, ninja, perl, python -ErrorAction Stop | Out-Null

foreach ($directory in @('manifest', 'triplets', 'toolchains', 'binary-cache')) {
    New-Item -ItemType Directory -Path "$SdkRoot/$directory" -Force | Out-Null
}
Copy-Item -LiteralPath "$PSScriptRoot/vcpkg.json" -Destination "$SdkRoot/manifest/vcpkg.json" -Force
Copy-Item -LiteralPath "$PSScriptRoot/$triplet.cmake" -Destination "$SdkRoot/triplets/$triplet.cmake" -Force
foreach ($file in @('msvc-x64-static-release.cmake', 'normalize-msvc-includes.cmake')) {
    Copy-Item -LiteralPath "$PSScriptRoot/$file" -Destination "$SdkRoot/toolchains/$file" -Force
}

if ($UseCachedSdk) {
    Assert-Qt $qtPrefix
    $receipt = Get-Content -Raw -LiteralPath "$SdkRoot/sdk/receipt.json" | ConvertFrom-Json
    if ($receipt.vcpkg -ne $vcpkgCommit -or $receipt.fluentQt -ne $fluentCommit -or $receipt.qtCppStandard -ne 20) { throw 'SDK cache revision/ABI mismatch.' }
    foreach ($file in @('lib/FluentQt.lib', 'lib/cmake/FluentQt/FluentQtConfig.cmake', 'share/licenses/FluentQt/LICENSE')) {
        if (-not (Test-Path -LiteralPath "$fluentPrefix/$file" -PathType Leaf)) { throw "Incomplete FluentQt cache: $file" }
    }
    Write-Host 'Reusing the cached Qt 6.11.2 / FluentQt 1.10.0 static SDK.'
    return
}

if (-not $ReuseInstalledDependencies) {
    $env:FLUENT_QT_PERL = (Get-Command perl).Source
    $env:FLUENT_QT_PYTHON = (Get-Command python).Source
    $env:VCPKG_BINARY_SOURCES = "clear;files,$SdkRoot/binary-cache,readwrite"
    $env:VCPKG_MAX_CONCURRENCY = '6'
    Invoke-Checked "$SdkRoot/vcpkg/bootstrap-vcpkg.bat" @('-disableMetrics')
    Invoke-Checked "$SdkRoot/vcpkg/vcpkg.exe" @(
        'install', "--triplet=$triplet", "--host-triplet=$triplet",
        "--x-manifest-root=$SdkRoot/manifest", "--x-install-root=$SdkRoot/installed",
        "--overlay-triplets=$SdkRoot/triplets", '--clean-after-build'
    )
}
Assert-Qt $qtPrefix

Invoke-Checked 'cmake' @(
    '-S', "$SdkRoot/Fluent-Qt", '-B', "$SdkRoot/build-fluent", '-G', 'Ninja',
    '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl', '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
    "-DCMAKE_TOOLCHAIN_FILE=$SdkRoot/toolchains/msvc-x64-static-release.cmake",
    "-DCMAKE_PREFIX_PATH=$qtPrefix", "-DCMAKE_INSTALL_PREFIX=$fluentPrefix",
    '-DBUILD_TESTING=OFF', '-DFLUENT_QT_BUILD_TESTS=OFF', '-DFLUENT_QT_BUILD_GALLERY=OFF',
    '-DFLUENT_QT_BUILD_EXAMPLES=OFF', '-DFLUENT_QT_BUILD_SPATIAL=OFF',
    '-DFLUENT_QT_BUILD_PLUGINS=OFF', '-DFLUENT_QT_BUILD_QML_BRIDGE=OFF',
    '-DFLUENT_QT_BUILD_PYSIDE6_BINDINGS=OFF', '-DFLUENT_QT_BUILD_WEBASSEMBLY_SUPPORT=OFF',
    '-DFLUENT_QT_INSTALL=ON', '-DFLUENT_QT_ENABLE_WARNINGS=ON'
)
Invoke-Checked 'cmake' @('--build', "$SdkRoot/build-fluent", '--target', 'FluentQt', '--parallel', '6')
Invoke-Checked 'cmake' @('--install', "$SdkRoot/build-fluent", '--config', 'Release', '--component', 'Development')
@{ qt = '6.11.2'; qtCppStandard = 20; fluentQt = $fluentCommit; vcpkg = $vcpkgCommit; crt = 'MT'; configuration = 'Release' } |
    ConvertTo-Json | Set-Content -LiteralPath "$SdkRoot/sdk/receipt.json" -Encoding utf8
Write-Host "Static SDK ready: $SdkRoot"
