param([ValidateSet('ARM64','x64')][string]$Platform='ARM64',[switch]$UseFrameworkRuntime,
      [string]$NuGetConfig=(Join-Path $PSScriptRoot 'NuGet.Config'))
$ErrorActionPreference='Stop'
$taskBuildPath=$env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:Path=$taskBuildPath
$env:DOTNET_CLI_HOME=Join-Path $PSScriptRoot '.dotnet-home'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
$project=Join-Path $PSScriptRoot 'source/App/CirDial.Settings/CirDial.Settings.csproj'
$folder=if($UseFrameworkRuntime){'FrameworkSettings'}else{'Settings'}
$output=Join-Path $PSScriptRoot "build/$Platform/$folder/"
$sdkSelfContained=(!$UseFrameworkRuntime.IsPresent).ToString().ToLowerInvariant()
& dotnet publish $project -c Release -r "win-$($Platform.ToLowerInvariant())" "/p:Platform=$Platform" "/p:WindowsAppSDKSelfContained=$sdkSelfContained" -o $output --configfile $NuGetConfig /nr:false
if($LASTEXITCODE){exit $LASTEXITCODE}
