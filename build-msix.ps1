param([ValidateSet('ARM64','x64')][string]$Platform='ARM64',
      [string]$IdentityName,[string]$Publisher,[string]$PublisherDisplayName,
      [switch]$Development,[switch]$UseFrameworkRuntime)
$ErrorActionPreference='Stop'
if($Development) {
 $IdentityName='CirDial.LocalPreview'
 $Publisher='CN=CirDial Local Preview'
 $PublisherDisplayName='CirDial Local Preview'
} elseif(!$IdentityName -or !$Publisher -or !$PublisherDisplayName) {
 throw 'Supply the real Partner Center Package Identity Name, Publisher and Publisher Display Name. No production identity is invented.'
}
$source=Join-Path $PSScriptRoot "build/$Platform"
$settingsFolder=if($UseFrameworkRuntime){'FrameworkSettings'}else{'Settings'}
$settings=Join-Path $source $settingsFolder
if(!(Test-Path (Join-Path $settings 'CirDial.Settings.exe'))){throw 'Build native and matching settings runtime configuration first'}
$native=if($Development){Join-Path $source 'VirtualDial.exe'}else{Join-Path $source 'UIAccess/VirtualDial.exe'}
if(!(Test-Path $native)){throw 'Build the production native executable with build-native.ps1 -UIAccess first'}
if(!$Development) {
 $storeConfig=Get-Content (Join-Path $settings 'store-config.json') -Raw|ConvertFrom-Json
 if(!$storeConfig.AppStoreId -or !$storeConfig.GestureProductStoreId){throw 'Production packaging requires genuine app and gesture add-on Store IDs in the built settings configuration.'}
 $usbConfig=Get-Content (Join-Path $PSScriptRoot 'source/Usb/CirDialUsbDeviceConfig.h') -Raw
 if($usbConfig -match 'VendorId\s*=\s*0x1209\s*,\s*ProductId\s*=\s*0x0*[01]\b'){throw 'The private test USB identifier cannot be used in a Store candidate. Obtain an approved production allocation and rebuild first.'}
 if((Get-Item $native).LastWriteTimeUtc -lt (Get-Item (Join-Path $PSScriptRoot 'source/Usb/CirDialUsbDeviceConfig.h')).LastWriteTimeUtc){throw 'Native executable predates USB identity configuration; rebuild before production packaging.'}
}
$stage=Join-Path $PSScriptRoot "packaging/staging/$Platform-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $stage|Out-Null
Copy-Item $native $stage
New-Item -ItemType Directory -Force (Join-Path $stage 'Settings')|Out-Null
Get-ChildItem -LiteralPath $settings|Copy-Item -Destination (Join-Path $stage 'Settings') -Recurse
Copy-Item (Join-Path $PSScriptRoot 'packaging/Assets') $stage -Recurse
# Development tests must never carry real purchase identifiers.
if($Development) {
 $configPath=Join-Path $stage 'Settings/store-config.json'
 $config=Get-Content $configPath -Raw|ConvertFrom-Json
 $config.GestureProductStoreId='';$config.AppStoreId=''
 $config|ConvertTo-Json|Set-Content $configPath -Encoding utf8
}
$manifest=Get-Content (Join-Path $PSScriptRoot 'packaging/AppxManifest.xml.template') -Raw
$manifest=$manifest.Replace('{{Name}}',[Security.SecurityElement]::Escape($IdentityName)).Replace('{{Publisher}}',[Security.SecurityElement]::Escape($Publisher)).Replace('{{PublisherDisplayName}}',[Security.SecurityElement]::Escape($PublisherDisplayName)).Replace('{{Architecture}}',$Platform.ToLowerInvariant())
$framework=if($UseFrameworkRuntime){'<PackageDependency Name="Microsoft.WindowsAppRuntime.1.8" Publisher="CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US" MinVersion="8000.994.2142.0" />'}else{''}
$manifest=$manifest.Replace('{{FrameworkDependency}}',$framework)
$manifest|Set-Content (Join-Path $stage 'AppxManifest.xml') -Encoding utf8
$suffix=if($Development){'LocalPreview'}else{'StoreCandidate'}
if($UseFrameworkRuntime){$suffix+='-FrameworkRuntime'}
$output=Join-Path $PSScriptRoot "packaging/CirDial-0.5.0-$Platform-$suffix.msix"
if(Test-Path $output){throw "Output exists; retain it and choose a new version before repacking: $output"}
& 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/arm64/makeappx.exe' pack /d $stage /p $output *> (Join-Path $PSScriptRoot "packaging/makeappx-$Platform-$suffix.log")
if($LASTEXITCODE){exit $LASTEXITCODE}
Write-Output "Unsigned package: $output. Packaging success does not verify deployment, driver installation, UIAccess, Store commerce, or certification."
