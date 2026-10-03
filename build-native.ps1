param([ValidateSet('ARM64','x64')][string]$Platform='ARM64',
      [string]$Packages=(Join-Path $PSScriptRoot 'source/packages/'),
      [switch]$UIAccess)
$ErrorActionPreference='Stop'
$taskBuildPath=$env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:Path=$taskBuildPath
$project=Join-Path $PSScriptRoot 'source/App/VirtualDial/VirtualDial.vcxproj'
$output=Join-Path $PSScriptRoot $(if($UIAccess){"build/$Platform/UIAccess/"}else{"build/$Platform/"})
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild=& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild/Current/Bin/MSBuild.exe' | Select-Object -First 1
if(!$msbuild){throw 'Visual Studio MSBuild was not found.'}
& $msbuild $project /t:Build "/p:Platform=$Platform" /p:Configuration=Release "/p:OutDir=$output" "/p:CirDialPackages=$Packages" "/p:CirDialUIAccess=$($UIAccess.IsPresent.ToString().ToLowerInvariant())" /p:TrackFileAccess=false /nr:false /v:minimal
if($LASTEXITCODE){exit $LASTEXITCODE}
