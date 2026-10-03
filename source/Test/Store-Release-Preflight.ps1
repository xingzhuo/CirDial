param(
 [Parameter(Mandatory=$true)][string]$Installer,
 [Parameter(Mandatory=$true)][string]$PayloadDirectory,
 [Parameter(Mandatory=$true)][uri]$DownloadUrl,
 [Parameter(Mandatory=$true)][string]$StoreProductId,
 [Parameter(Mandatory=$true)][string]$PublisherThumbprint
)
$ErrorActionPreference='Stop'
$failures=[Collections.Generic.List[string]]::new()
if([IO.Path]::GetExtension($Installer) -notin @('.exe','.msi')){$failures.Add('Store installer must be EXE or MSI, not a ZIP/CMD/PS1.')}
if($DownloadUrl.Scheme -ne 'https'){$failures.Add('Installer URL must use HTTPS.')}
if($StoreProductId -match 'PLACEHOLDER|TODO|^$'){$failures.Add('Store product ID is missing.')}
if($PublisherThumbprint -eq '960C57E031A2A73846847728836A1F6D75F08700'){$failures.Add('Local development certificate is not a production publisher certificate.')}
$signature=Get-AuthenticodeSignature -LiteralPath $Installer
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $PublisherThumbprint){$failures.Add('Installer signature is invalid or does not match the release publisher.')}
foreach($file in Get-ChildItem -LiteralPath $PayloadDirectory -File -Recurse | Where-Object Extension -in @('.exe','.dll','.sys')) {
 $sig=Get-AuthenticodeSignature -LiteralPath $file.FullName
 if($sig.Status -ne 'Valid'){$failures.Add('Unsigned or invalid PE: '+$file.FullName)}
}
if(!(Get-ChildItem -LiteralPath $PayloadDirectory -File -Recurse | Where-Object Name -eq 'THIRD-PARTY-NOTICES.md')){$failures.Add('Third-party notices are missing.')}
if($failures.Count){$failures|Write-Output;throw 'Release preflight failed.'}
Write-Output 'Basic file checks passed. CA trust chain, timestamp, VID/PID rights, hosted URL identity, offline/silent install, upgrade, uninstall, licensing and Store certification still require release validation.'
