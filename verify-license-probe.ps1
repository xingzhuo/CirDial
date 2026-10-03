param([ValidateSet('ARM64','x64')][string]$Platform='ARM64')
$ErrorActionPreference='Stop'
$name='CirDial.License.test.'+[Guid]::NewGuid().ToString('N')
$pipe=[IO.Pipes.NamedPipeServerStream]::new($name,[IO.Pipes.PipeDirection]::In,1,[IO.Pipes.PipeTransmissionMode]::Byte,[IO.Pipes.PipeOptions]::Asynchronous)
try {
 $connection=$pipe.WaitForConnectionAsync()
 $exe=Join-Path $PSScriptRoot "build/$Platform/Settings/CirDial.Settings.exe"
 $process=Start-Process $exe -ArgumentList @('--license-check',$name) -WindowStyle Hidden -PassThru
 if(-not $connection.Wait(10000)){throw 'License probe failed to connect'}
 $bytes=[byte[]]::new(1)
 $read=$pipe.ReadAsync($bytes,0,1)
 if(-not $read.Wait(5000)){throw 'License probe failed to reply'}
 if($read.Result -ne 1 -or $bytes[0] -ne 0){throw 'Unconfigured Store license must deny entitlement'}
 if(-not $process.WaitForExit(10000)){throw 'License probe did not exit'}
 if($process.ExitCode -ne 0){throw "License probe exit $($process.ExitCode)"}
 $result=[pscustomobject]@{Platform=$Platform;Test='Unconfigured Store denies entitlement through pipe';Reply=$bytes[0];ExitCode=$process.ExitCode}
 $result|ConvertTo-Json|Set-Content (Join-Path $PSScriptRoot "build/$Platform/license-probe-validation.json")
 $result|Format-List
} finally {$pipe.Dispose()}
