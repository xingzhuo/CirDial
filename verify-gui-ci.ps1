param(
    [ValidateSet('x64', 'ARM64')][string]$Platform = 'x64',
    [ValidateRange(5, 90)][int]$TimeoutSeconds = 35
)
$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$output = Join-Path $root "build/$Platform"
$native = Join-Path $output 'VirtualDial.exe'
$settings = Join-Path $output 'Settings/CirDial.Settings.exe'
$visuals = Join-Path $output 'Visuals'
$settingsPreview = Join-Path $output 'Settings/Settings-preview.png'
$reportPath = Join-Path $output 'gui-validation.json'
$results = [Collections.Generic.List[object]]::new()
$issues = [Collections.Generic.List[string]]::new()

New-Item -ItemType Directory -Path $output -Force | Out-Null

function Invoke-Check {
    param([string]$File, [string]$Argument, [string]$Name)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $code = -1
    try {
        if (!(Test-Path -LiteralPath $File -PathType Leaf)) {
            throw "Executable not found: $File"
        }
        $process = Start-Process -FilePath $File -ArgumentList $Argument -PassThru -WindowStyle Normal
        try {
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                throw "Timed out after $TimeoutSeconds seconds"
            }
            $process.Refresh()
            $code = $process.ExitCode
            if ($code -ne 0) { throw "Exit code $code" }
        } finally {
            $process.Dispose()
        }
        Write-Host "PASS: $Name"
    } catch {
        $issues.Add("$($Name): $($_.Exception.Message)")
        Write-Warning "FAIL: $Name - $($_.Exception.Message)"
    } finally {
        $watch.Stop()
        $results.Add([pscustomobject]@{
            Name = $Name
            ExitCode = $code
            ElapsedSeconds = [math]::Round($watch.Elapsed.TotalSeconds, 2)
        })
    }
}

function Confirm-Png {
    param([string]$File, [string]$Name)
    if (!(Test-Path -LiteralPath $File -PathType Leaf)) {
        $issues.Add("Missing PNG ($Name): $File")
        return $null
    }
    $bytes = [IO.File]::ReadAllBytes($File)
    if ($bytes.Length -lt 100 -or [BitConverter]::ToString($bytes, 0, 8) -ne '89-50-4E-47-0D-0A-1A-0A') {
        $issues.Add("Invalid PNG ($Name): $File")
        return $null
    }
    # IHDR width and height are big-endian integers at offsets 16 and 20.
    $width = [int](([uint32]$bytes[16] -shl 24) -bor ([uint32]$bytes[17] -shl 16) -bor ([uint32]$bytes[18] -shl 8) -bor [uint32]$bytes[19])
    $height = [int](([uint32]$bytes[20] -shl 24) -bor ([uint32]$bytes[21] -shl 16) -bor ([uint32]$bytes[22] -shl 8) -bor [uint32]$bytes[23])
    if ($width -lt 16 -or $height -lt 16) {
        $issues.Add("Invalid PNG dimensions ($Name): $width x $height")
        return $null
    }
    return [pscustomobject]@{
        File = [IO.Path]::GetFileName($File)
        Width = $width
        Height = $height
        Bytes = $bytes.Length
        Sha256 = (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash
    }
}

$pngs = [Collections.Generic.List[object]]::new()
try {
    Invoke-Check -File $native -Argument '--interaction-test' -Name 'Native modeled touch and gesture checks'
    Invoke-Check -File $native -Argument '--visual-test' -Name 'Native rendered visual checks'

    # The native renderer creates four states and eight reveal-transition frames.
    $expected = @('0.png', '1.png', '2.png', '3.png')
    foreach ($prefix in @('show-', 'hide-')) {
        foreach ($frame in 1..4) { $expected += "$prefix$frame.png" }
    }
    foreach ($name in $expected) {
        $item = Confirm-Png -File (Join-Path $visuals $name) -Name $name
        if ($null -ne $item) { $pngs.Add($item) }
    }

    Invoke-Check -File $settings -Argument '--ui-test' -Name 'WinUI settings behavior checks'
    Invoke-Check -File $settings -Argument '--render-preview' -Name 'WinUI screenshot capture'
    $preview = Confirm-Png -File $settingsPreview -Name 'Settings-preview.png'
    if ($null -ne $preview) { $pngs.Add($preview) }

    $diagnostic = Join-Path $env:LOCALAPPDATA 'CirDial/settings-error.txt'
    if (Test-Path -LiteralPath $diagnostic) {
        $target = Join-Path $output 'gui-diagnostics'
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item -LiteralPath $diagnostic -Destination (Join-Path $target 'settings-error.txt') -Force
    }
} finally {
    $report = [ordered]@{
        Platform = $Platform
        Scope = 'Internal simulated Win32/WinUI checks and in-process rendered PNGs; NOT physical touch, actual desktop screenshots, USB/IP, UIAccess or native Surface Dial menu validation.'
        Checks = @($results.ToArray())
        Pngs = @($pngs.ToArray())
        Issues = @($issues.ToArray())
    }
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding utf8
    Write-Host "Saved GUI verification: $reportPath"
}

if ($issues.Count -ne 0) { throw "$($issues.Count) GUI verification issue(s). See $reportPath and uploaded artifacts." }
Write-Host "PASS: $($pngs.Count) verified PNG files and $($results.Count) GUI checks for $Platform"
