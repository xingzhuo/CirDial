param([ValidateSet('ARM64','x64')][string]$Platform='ARM64',[switch]$IncludeUI)
$ErrorActionPreference='Stop'
$results=@()
$native=Join-Path $PSScriptRoot "build/$Platform/VirtualDial.exe"
foreach($test in @('--self-test','--interaction-test','--visual-test')) {
    $process=Start-Process -FilePath $native -ArgumentList $test -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(30000)){Stop-Process -Id $process.Id;throw "$Platform $test timeout"}
    $results+=[pscustomobject]@{Platform=$Platform;Test=$test;ExitCode=$process.ExitCode}
}
if($IncludeUI) {
    $settings=Join-Path $PSScriptRoot "build/$Platform/Settings/CirDial.Settings.exe"
    $process=Start-Process -FilePath $settings -ArgumentList '--ui-test' -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(30000)){Stop-Process -Id $process.Id;throw "$Platform UI test timeout"}
    $results+=[pscustomobject]@{Platform=$Platform;Test='--ui-test';ExitCode=$process.ExitCode}
}
$results|ConvertTo-Json|Set-Content (Join-Path $PSScriptRoot "build/$Platform/validation.json")
$results|Format-Table
if($results|Where-Object ExitCode -ne 0){exit 1}
