param([string]$UsbIpDirectory=(Join-Path $env:ProgramFiles 'USBip'))
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
& (Join-Path $root 'Get-UsbIpPocStatus.ps1') -ReportPath (Join-Path $root 'poc-status-before.json')|Out-Null
$status=Get-Content (Join-Path $root 'poc-status-before.json') -Raw|ConvertFrom-Json
if(!$status.EnvironmentGatePassed){throw 'Environment gate failed or unknown. Read poc-status-before.json. This script will not change Secure Boot, test signing, or existing drivers.'}
if(!$status.TransportRunning){throw 'Install the verified official usbip-win2 package first. No transport was installed by this script.'}
$arch=if($status.Architecture -eq 'Arm64'){'ARM64'}elseif($status.Architecture -eq 'X64'){'x64'}else{throw 'Unsupported native architecture'}
$pins=Get-Content (Join-Path $root 'transport-pins.json') -Raw|ConvertFrom-Json
$selectedPins=@($pins|Where-Object Architecture -eq $arch)
if($selectedPins.Count -ne 5){throw 'Incomplete transport allowlist'}
foreach($entry in $selectedPins) {
 $file=if($entry.Kind -eq 'user'){Join-Path $UsbIpDirectory $entry.File}else{
  $d=@($status.Drivers|Where-Object Name -eq ([IO.Path]::GetFileNameWithoutExtension($entry.File)))
  if($d.Count -ne 1){throw "Missing transport driver $($entry.File)"}
  $d[0].PathName.Trim('"').Replace('\SystemRoot\',"$env:SystemRoot\")
 }
 if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.SHA256){throw "Unverified transport version or content: $file"}
}
$cli=Join-Path $UsbIpDirectory 'usbip.exe'
function Invoke-UsbIp([string[]]$Arguments,[string]$Name) {
 $out=Join-Path $root "$Name.log";$err=Join-Path $root "$Name-error.log"
 $p=Start-Process $cli -ArgumentList $Arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err
 if(!$p.WaitForExit(20000)){$p.Kill();throw "USBip command timed out: $Name"}
 if($p.ExitCode){throw "USBip command failed: $Name, see $out and $err"}
 Get-Content $out -Raw
}
# An exclusive bind avoids replacing any existing server or session.
$probe=New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback,3240)
try{$probe.Server.ExclusiveAddressUse=$true;$probe.Start()}finally{$probe.Stop()}
function Invoke-HidProbe([string]$Name) {
 $p=Start-Process (Join-Path $root "$arch/Release/DialTest.exe") -ArgumentList '--usbip-hid-probe' -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root "$Name.json") -RedirectStandardError (Join-Path $root "$Name-error.log")
 if(!$p.WaitForExit(15000)){$p.Kill();Write-Warning 'HID capability query timed out';return}
 if($p.ExitCode){Write-Warning "HID capability query not yet passed: $Name.json"}
}
$server=$null;$port=$null;$stdout=$null;$stderr=$null
try {
 $si=New-Object System.Diagnostics.ProcessStartInfo
 $si.FileName=Join-Path $root "$arch/Release/DialTest.exe"
 $si.Arguments='--backend usbip interactive'
 $si.UseShellExecute=$false;$si.CreateNoWindow=$true
 $si.RedirectStandardInput=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
 $server=New-Object System.Diagnostics.Process;$server.StartInfo=$si
 if(!$server.Start()){throw 'Could not start input server'}
 $stdout=$server.StandardOutput.ReadToEndAsync();$stderr=$server.StandardError.ReadToEndAsync()
 Start-Sleep -Milliseconds 400
 if($server.HasExited){throw 'CirDial USB server failed to start; inspect poc-server.log'}
 Invoke-UsbIp @('list','-r','127.0.0.1') 'poc-list'|Write-Output
 $result=Invoke-UsbIp @('attach','-r','127.0.0.1','-b','1-1','--once','--terse') 'poc-attach'
 if($result.Trim() -notmatch '^[1-9][0-9]*$'){throw 'Attach did not return a port; do not detach other devices. Inspect poc-attach.log.'}
 $port=[int]$result.Trim()
 Write-Output "Attached Phase 4 to port $port. Checking HID before allowing input."
 Start-Sleep -Seconds 5
 & (Join-Path $root 'Get-UsbIpPocStatus.ps1') -ReportPath (Join-Path $root 'poc-status-attached.json')|Out-Null
 Invoke-HidProbe 'poc-hid-probe'
 $hid=Get-Content (Join-Path $root 'poc-hid-probe.json') -Raw|ConvertFrom-Json
 $attached=Get-Content (Join-Path $root 'poc-status-attached.json') -Raw|ConvertFrom-Json
 if(!$hid.passed -or !$attached.EnvironmentGatePassed -or !$attached.Phase2HidBindingPassed){throw 'HID/environment validation failed. No input sent.'}
 Write-Output 'Commands: h=hold 1.2s (open menu), c=clockwise 15deg, a=counterclockwise 15deg, p=click, u=release, q=finish.'
 Write-Output 'Only your selected command sends input. Start with h; observe the menu, then c/a, then p. Limit: 60 commands.'
 for($i=0;$i -lt 60;$i++) {
  $choice=(Read-Host 'h / c / a / p / u / q').Trim().ToLowerInvariant()
  if($choice -eq 'q'){break}
  $command=switch($choice){'h'{'hold'} 'c'{'cw'} 'a'{'ccw'} 'p'{'click'} 'u'{'up'} default{''}}
  if(!$command){Write-Output 'Unknown command';continue}
  if($server.HasExited){throw 'Input server exited unexpectedly'}
  $server.StandardInput.WriteLine($command);$server.StandardInput.Flush()
  Start-Sleep -Milliseconds 1500
 }
 & (Join-Path $root 'Get-UsbIpPocStatus.ps1') -ReportPath (Join-Path $root 'poc-status-final-attached.json')|Out-Null
 Invoke-HidProbe 'poc-hid-probe-final'
}finally {
 if($null -ne $port){try{Invoke-UsbIp @('detach','-p',"$port") 'poc-detach'|Write-Output}catch{Write-Warning $_.Exception.Message}}
 if($server -and !$server.HasExited){
  try{$server.StandardInput.WriteLine('quit');$server.StandardInput.Close()}catch{}
  if(!$server.WaitForExit(10000)){$server.Kill();$server.WaitForExit()}
 }
 if($stdout){[IO.File]::WriteAllText((Join-Path $root 'poc-server.log'),$stdout.GetAwaiter().GetResult())}
 if($stderr){[IO.File]::WriteAllText((Join-Path $root 'poc-server-error.log'),$stderr.GetAwaiter().GetResult())}
 & (Join-Path $root 'Get-UsbIpPocStatus.ps1') -ReportPath (Join-Path $root 'poc-status-after.json')|Out-Null
}
