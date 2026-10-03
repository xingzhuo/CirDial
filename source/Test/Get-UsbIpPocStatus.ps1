param([string]$ReportPath=(Join-Path $PSScriptRoot 'poc-status.json'))
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class CirDialCodeIntegrity {
 [StructLayout(LayoutKind.Sequential)] public struct Info { public uint Length; public uint Options; }
 [DllImport("ntdll.dll")] public static extern int NtQuerySystemInformation(int cls,ref Info info,int size,out int returned);
}
'@ -ErrorAction SilentlyContinue
$ci=New-Object CirDialCodeIntegrity+Info;$ci.Length=8;$returned=0
$nt=[CirDialCodeIntegrity]::NtQuerySystemInformation(103,[ref]$ci,8,[ref]$returned)
$testSigning=if($nt -eq 0){($ci.Options -band 2) -ne 0}else{$null}
$secure=$null;$secureError=''
try{$secure=Confirm-SecureBootUEFI}catch{$secureError=$_.Exception.Message}
$drivers=@();$driverError=''
try{$drivers=@(Get-CimInstance Win32_SystemDriver | Where-Object {$_.Name -in @('VirtualDialDriver','usbip2_ude','usbip2_filter')} | Select-Object Name,State,PathName,StartMode)}catch{$driverError=$_.Exception.Message}
$vhfRunning=@($drivers|Where-Object {$_.Name -eq 'VirtualDialDriver' -and $_.State -eq 'Running'}).Count -gt 0
$transportRunning=@($drivers|Where-Object {$_.Name -eq 'usbip2_ude' -and $_.State -eq 'Running'}).Count -gt 0
$vhfRunning=if($driverError){$null}else{$vhfRunning}
$transportRunning=if($driverError){$null}else{$transportRunning}
$deviceError='';$devices=@()
try{$devices=@(Get-CimInstance Win32_PnPEntity|Where-Object {$_.PNPDeviceID -match '^(USB|HID)\\VID_1209&PID_0001'}|Select-Object Name,PNPDeviceID,ConfigManagerErrorCode,Service,Present)}catch{$deviceError=$_.Exception.Message}
$bindingError='';$bindings=@()
try{$bindings=@(Get-CimInstance Win32_PnPSignedDriver|Where-Object {$_.DeviceID -match '^(USB|HID)\\VID_1209&PID_0001'}|Select-Object DeviceID,DriverProviderName,InfName,DriverVersion,IsSigned)}catch{$bindingError=$_.Exception.Message}
$report=[pscustomobject]@{
 Architecture=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
 SecureBoot=$secure;SecureBootReadError=$secureError
 TestSigning=$testSigning;CodeIntegrityQueryStatus=$nt;CodeIntegrityOptions=$ci.Options
 CirDialDriverRunning=$vhfRunning;TransportRunning=$transportRunning;Drivers=$drivers;DriverQueryError=$driverError
 UsbDevices=$devices;DeviceQueryError=$deviceError;Bindings=$bindings;BindingQueryError=$bindingError
 EnvironmentGatePassed=($secure -eq $true -and $testSigning -eq $false -and !$vhfRunning -and !$driverError -and $nt -eq 0)
 Phase2HidBindingPassed=(!$deviceError -and @($devices|Where-Object {$_.Service -eq 'HidUsb' -and $_.ConfigManagerErrorCode -eq 0 -and $_.Present -eq $true}).Count -gt 0 -and @($devices|Where-Object {$_.PNPDeviceID -like 'HID\*' -and $_.ConfigManagerErrorCode -eq 0 -and $_.Present -eq $true}).Count -gt 0)
 Note='Read-only snapshot. Phase 2 requires HidUsb and HID child Code 0; not proof of Dial input support.'
}
$report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $ReportPath -Encoding UTF8
$report
