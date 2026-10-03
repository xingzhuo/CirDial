param([Parameter(Mandatory=$true)][string]$Installer)
$ErrorActionPreference='Stop'
$arch=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
$expected=switch($arch){
 'Arm64'{'CAB7FF97F79275EEB5C5B8BB2EB3111FF6B6C4EEAD07D9BEC9BE5BD1E3A35800'}
 'X64'{'38CAD6D4432B52D5BB9409D9AD03B72FDFFC4ADA4CD3A48FBECA1A2752A8518A'}
 default{throw 'Unsupported architecture'}
}
$name="USBip-0.9.8.1-$($arch.ToLower()).exe"
if((Split-Path $Installer -Leaf) -ne $name){throw "Expected official filename $name"}
if((Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash -ne $expected){throw 'Official release SHA-256 mismatch; do not install.'}
$signature=Get-AuthenticodeSignature -LiteralPath $Installer
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike '*CN=Cloudyne Systems (Scheibling Consulting AB),*'){throw 'Unexpected or invalid installer publisher signature; do not install.'}
Write-Output 'PASS: pinned official 0.9.8.1 installer bytes and Authenticode publisher. No installation was performed.'
