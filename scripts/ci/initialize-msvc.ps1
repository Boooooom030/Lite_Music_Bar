$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere is required.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'The MSVC x64 toolset was not found.' }
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$environment = & cmd.exe /d /s /c ('call "{0}" >nul && set' -f $vcvars)
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed.' }

# Export only compiler/SDK environment, never credentials or runner controls.
$names = @('PATH', 'INCLUDE', 'LIB', 'LIBPATH', 'VSINSTALLDIR', 'VCINSTALLDIR',
    'VCTOOLSINSTALLDIR', 'VCTOOLSVERSION', 'VCTOOLSREDISTDIR', 'VCIDEINSTALLDIR',
    'VISUALSTUDIOVERSION', 'DEVENVDIR', 'WINDOWSSDKDIR', 'WINDOWSSDKVERSION',
    'WINDOWSSDKBINPATH', 'WINDOWSSDKVERBINPATH', 'WINDOWSLIBPATH',
    'UCRTVERSION', 'UNIVERSALCRTSDKDIR', 'EXTENSIONSDKDIR', 'NETFXSDKDIR')
$exports = [Collections.Generic.List[string]]::new()
foreach ($line in $environment) {
    $separator = $line.IndexOf('=')
    if ($separator -le 0) { continue }
    $name = $line.Substring(0, $separator)
    if ($name.ToUpperInvariant() -notin $names -and $name -notmatch '^(__)?VSCMD_') { continue }
    $value = $line.Substring($separator + 1)
    [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    $exports.Add("$name=$value")
}
Get-Command cl, link, rc -ErrorAction Stop | Out-Null
if (-not $env:VCToolsVersion -or -not $env:WindowsSDKVersion) { throw 'The MSVC/Windows SDK version is missing.' }
if (-not $env:GITHUB_ENV) { throw 'GITHUB_ENV must name the CI environment output file.' }
[IO.File]::AppendAllLines($env:GITHUB_ENV, $exports, [Text.UTF8Encoding]::new($false))
Write-Host "MSVC x64 $env:VCToolsVersion / Windows SDK $env:WindowsSDKVersion"
