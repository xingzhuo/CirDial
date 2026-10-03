param([ValidateSet('ARM64','x64')][string]$Platform='ARM64',
      [ValidateRange(10,40)][int]$Seconds=20)
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot "build/$Platform/VirtualDial.exe"
$process=Start-Process -FilePath $exe -ArgumentList '--preview' -WindowStyle Hidden -PassThru
try {
 Start-Sleep -Seconds 2
 $process.Refresh()
 if($process.HasExited){throw 'Preview exited; another preview may already be running.'}
 $cpuStart=$process.TotalProcessorTime.TotalMilliseconds
 $timer=[Diagnostics.Stopwatch]::StartNew()
 $samples=@()
 for($index=0;$index -lt $Seconds;$index++) {
  Start-Sleep -Seconds 1
  $process.Refresh()
  if($process.HasExited){throw 'Preview exited during measurement.'}
  $samples+=[pscustomobject]@{WorkingSetBytes=$process.WorkingSet64;PrivateBytes=$process.PrivateMemorySize64;Handles=$process.HandleCount;Threads=$process.Threads.Count}
 }
 $timer.Stop()
 $cpu=$process.TotalProcessorTime.TotalMilliseconds-$cpuStart
 $result=[pscustomobject]@{
  Scope='Idle visible preview only: no driver, global gesture, Store probe or physical input. Not a production benchmark.'
  Platform=$Platform;Seconds=$timer.Elapsed.TotalSeconds;CpuMilliseconds=$cpu
  CpuPercentOfOneCore=100*$cpu/$timer.Elapsed.TotalMilliseconds
  Samples=$samples
  ExecutableSHA256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
 }
 $output=Join-Path $PSScriptRoot "build/$Platform/preview-resource-measurement.json"
 $result|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $output -Encoding utf8
 Write-Output "Saved: $output"
 $result|Select-Object Scope,Seconds,CpuMilliseconds,CpuPercentOfOneCore|Format-List
} finally {
 if(!$process.HasExited){$null=$process.CloseMainWindow();if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}}
 $process.Dispose()
}
