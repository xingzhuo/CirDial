param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('ARM64', 'x64')][string]$Platform = 'ARM64',
    [switch]$Restore
)
$ErrorActionPreference = 'Stop'
if ($Restore) {
    & nuget.exe restore (Join-Path $PSScriptRoot 'packages.config') -PackagesDirectory (Join-Path $PSScriptRoot 'packages') -Source 'https://api.nuget.org/v3/index.json' -NonInteractive
    if ($LASTEXITCODE -ne 0) { throw 'NuGet restore failed.' }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$hostArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
$hostBin = if ($hostArchitecture -eq 'Arm64') { 'arm64' } else { 'amd64' }
$msbuild = & $vswhere -latest -version '[18.0,19.0)' -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\$hostBin\MSBuild.exe"
if (-not $msbuild) { throw 'Visual Studio 2026 with C++ and the Windows Driver Kit component is required.' }
$logs = Join-Path $PSScriptRoot 'build-logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
& $msbuild (Join-Path $PSScriptRoot 'VirtualDial.sln') /t:Rebuild /m /nr:false "/p:Configuration=$Configuration" "/p:Platform=$Platform" /p:SignMode=Off /nologo /v:minimal "/flp:logfile=$logs\$Platform-$Configuration.log;verbosity=normal;encoding=UTF-8"
if ($LASTEXITCODE -ne 0) { throw "Build failed. See $logs\$Platform-$Configuration.log" }
