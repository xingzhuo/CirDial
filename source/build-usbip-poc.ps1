param([string]$OutputDirectory=(Join-Path $PSScriptRoot 'poc-build'))
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$hostBin=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -eq 'Arm64'){'arm64'}else{'amd64'}
$msbuild=& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild/Current/Bin/$hostBin/MSBuild.exe"
if(-not $msbuild){throw 'MSBuild/C++ tools are required.'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$buildPath=$env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:Path=$buildPath
foreach($arch in @('ARM64','x64')) {
 foreach($config in @('Debug','Release')) {
  $out=Join-Path $OutputDirectory "$arch/$config"
  New-Item -ItemType Directory -Force $out|Out-Null
  & $msbuild (Join-Path $PSScriptRoot 'Test/DialTest/DialTest.vcxproj') /t:Rebuild "/p:Platform=$arch" "/p:Configuration=$config" "/p:OutDir=$out\" /p:TrackFileAccess=false /nr:false /v:minimal "/flp:logfile=$out/build.log;verbosity=normal"
  if($LASTEXITCODE){throw "Build failed: $arch $config"}
 }
}

Copy-Item (Join-Path $PSScriptRoot 'Test/Get-UsbIpPocStatus.ps1'),(Join-Path $PSScriptRoot 'Test/Run-UsbIpPoc.ps1'),(Join-Path $PSScriptRoot 'Test/Verify-UsbIpInstaller.ps1'),(Join-Path $PSScriptRoot 'Test/transport-pins.json') $OutputDirectory -Force
