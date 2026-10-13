[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$SdkRoot,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SourceRevision = 'local'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = Join-Path $BuildDirectory 'MusicBar.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing executable: $exe" }
$size = (Get-Item -LiteralPath $exe).Length
if ($size -ge 100000000) { throw "MusicBar.exe exceeds the 100 MB limit: $size bytes" }

# Static Qt and /MT must not silently become a DLL/VC++ redistributable build.
$dependencies = & dumpbin /nologo /dependents $exe
if ($LASTEXITCODE -ne 0) { throw 'dumpbin could not inspect MusicBar.exe.' }
$imports = @($dependencies | ForEach-Object {
    if ($_ -match '^\s+([\w.-]+\.dll)\s*$') { $Matches[1] }
})
if ($imports.Count -eq 0) { throw 'No PE imports were found; dependency verification failed.' }
if ($imports -match '^(Qt\d|FluentQt|MSVCP\d|VCRUNTIME\d|CONCRT\d|ucrtbase)') {
    throw "Unexpected runtime DLL dependency: $($imports -join ', ')"
}

$package = Join-Path $OutputDirectory 'package'
# Refuse stale content instead of accidentally publishing other EXEs/DLLs.
if (Test-Path -LiteralPath $package) { throw "Package directory already exists: $package; use a fresh output directory." }
$portable = Join-Path $package 'MusicBar'
$licenses = Join-Path $portable 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination "$portable/MusicBar.exe"
Copy-Item -LiteralPath "$repo/assets/portable/README.txt" -Destination "$portable/README.txt"
Copy-Item -LiteralPath "$repo/third_party/nlohmann/LICENSE.MIT" -Destination "$licenses/nlohmann-json.MIT.txt"
Copy-Item -LiteralPath "$repo/third_party/miniz/LICENSE.MIT" -Destination "$licenses/miniz.MIT.txt"
Copy-Item -LiteralPath "$repo/docs/THIRD_PARTY.md" -Destination "$licenses/THIRD_PARTY.md"
Copy-Item -LiteralPath "$SdkRoot/sdk/FluentQt/share/licenses/FluentQt" -Destination "$licenses/FluentQt" -Recurse
$qtShare = Join-Path $SdkRoot 'installed/x64-windows-static-release/share'
foreach ($required in @('qtbase', 'harfbuzz', 'freetype', 'libpng', 'libjpeg-turbo', 'zlib')) {
    if (-not (Test-Path -LiteralPath "$qtShare/$required/copyright" -PathType Leaf)) {
        throw "Missing dependency license: $required"
    }
}
foreach ($component in Get-ChildItem -LiteralPath $qtShare -Directory) {
    $copyright = Join-Path $component.FullName 'copyright'
    if (Test-Path -LiteralPath $copyright -PathType Leaf) {
        $destination = Join-Path $licenses "Qt-dependencies/$($component.Name)"
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Copy-Item -LiteralPath $copyright -Destination "$destination/copyright.txt"
    }
}
$sdk = Get-Content -Raw -LiteralPath "$SdkRoot/sdk/receipt.json" | ConvertFrom-Json
@{
    sourceRevision = $SourceRevision; executableBytes = $size
    sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    qt = $sdk.qt; fluentQt = $sdk.fluentQt; vcpkg = $sdk.vcpkg
    crt = $sdk.crt; configuration = $sdk.configuration; imports = $imports
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$portable/build-info.json" -Encoding utf8
Compress-Archive -Path "$package/*" -DestinationPath "$OutputDirectory/MusicBar-Windows-x64.zip"
$zipHash = (Get-FileHash -LiteralPath "$OutputDirectory/MusicBar-Windows-x64.zip" -Algorithm SHA256).Hash.ToLowerInvariant()
"$zipHash  MusicBar-Windows-x64.zip" | Set-Content -LiteralPath "$OutputDirectory/MusicBar-Windows-x64.zip.sha256" -Encoding ascii
Write-Host "Packaged MusicBar.exe: $size bytes; imports: $($imports -join ', ')"
