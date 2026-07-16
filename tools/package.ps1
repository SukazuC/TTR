param(
  [Parameter(Mandatory = $true)][string]$BuildDirectory,
  [Parameter(Mandatory = $true)][string]$SourceDirectory,
  [Parameter(Mandatory = $true)][string]$OutputDirectory,
  [Parameter(Mandatory = $true)][string]$Dumpbin,
  [Parameter(Mandatory = $true)][string]$PrivateKey,
  [string]$KeyDescription = 'personal local public key',
  [switch]$AllowUnsupportedMachine
)

$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath($SourceDirectory)
$output = [IO.Path]::GetFullPath($OutputDirectory)
$allowed = [IO.Path]::GetFullPath((Join-Path $source 'out'))
if (-not $output.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Package output must remain inside the repository out directory.'
}
if (Test-Path -LiteralPath $output) {
  Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path $output | Out-Null

$hostExe = Join-Path $BuildDirectory 'TaskbarThumbnailReorder.exe'
$payload = Join-Path $BuildDirectory 'TTRHook64.dll'
$resourcecheck = Join-Path $BuildDirectory 'resourcecheck.exe'
& $resourcecheck $hostExe $payload `
  (Join-Path $source 'compat\qualified\compat.bin') `
  (Join-Path $source 'compat\qualified\compat.sig') `
  (Join-Path $source 'compat\qualified\manifest-public-key.bin')
if ($LASTEXITCODE -ne 0) { throw 'resource verification failed' }

$dependencies = & $Dumpbin /dependents $hostExe | Out-String
if ($dependencies -match '(?i)VCRUNTIME|MSVCP\d') {
  throw 'Release host has an accidental dynamic Visual C++ runtime dependency.'
}

$requiredSizes = @(16, 20, 24, 32, 48, 256)
foreach ($iconName in @('icon-enabled.ico', 'icon-disabled.ico', 'icon-warning.ico')) {
  $iconPath = Join-Path $source "host\resources\$iconName"
  $bytes = [IO.File]::ReadAllBytes($iconPath)
  $count = [BitConverter]::ToUInt16($bytes, 4)
  $sizes = for ($index = 0; $index -lt $count; ++$index) {
    $width = [int]$bytes[6 + $index * 16]
    if ($width -eq 0) { $width = 256 }
    $width
  }
  if ($count -ne $requiredSizes.Count -or (Compare-Object $requiredSizes $sizes)) {
    throw "$iconName does not contain the required icon resolutions"
  }
}

$hostBytes = [IO.File]::ReadAllBytes($hostExe)
$privateBytes = [IO.File]::ReadAllBytes($PrivateKey)
function Find-Sequence([byte[]]$Haystack, [byte[]]$Needle) {
  for ($start = 0; $start -le $Haystack.Length - $Needle.Length; ++$start) {
    $match = $true
    for ($index = 0; $index -lt $Needle.Length; ++$index) {
      if ($Haystack[$start + $index] -ne $Needle[$index]) { $match = $false; break }
    }
    if ($match) { return $start }
  }
  return -1
}
if ((Find-Sequence $hostBytes $privateBytes) -ge 0) {
  throw 'Private key bytes were found in the distributable executable.'
}

$packageExe = Join-Path $output 'TaskbarThumbnailReorder.exe'
Copy-Item -LiteralPath $hostExe -Destination $packageExe
$diagnosticOut = Join-Path $output 'diagnostic.json'
$diagnosticErr = Join-Path $output 'diagnostic.stderr.txt'
$process = Start-Process -FilePath $packageExe -ArgumentList @('--diagnose-offline', '--json') `
  -WorkingDirectory $output -NoNewWindow -Wait -PassThru `
  -RedirectStandardOutput $diagnosticOut -RedirectStandardError $diagnosticErr
$diagnostic = Get-Content -Raw -LiteralPath $diagnosticOut | ConvertFrom-Json
$supported = $process.ExitCode -eq 0 -and $diagnostic.result -eq 'pass' -and
  $diagnostic.compatibility_status -eq 'supported' -and $diagnostic.record_matched -eq $true
$safeUnsupported = $AllowUnsupportedMachine -and $process.ExitCode -eq 6 -and
  $diagnostic.result -eq 'fail' -and $diagnostic.compatibility_status -eq 'unsupported' -and
  $diagnostic.record_matched -eq $false -and $diagnostic.manifest_valid -eq $true -and
  $diagnostic.embedded_manifest_signature_valid -eq $true
if ($diagnostic.live_integration -ne $false -or (-not $supported -and -not $safeUnsupported)) {
  throw "packaged offline diagnostic did not pass safely: $($process.ExitCode)"
}
Remove-Item -LiteralPath $diagnosticOut, $diagnosticErr -Force

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $packageExe).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'),
  "$hash  TaskbarThumbnailReorder.exe`n", [Text.UTF8Encoding]::new($false))

$report = @"
# Build report

- Project version: 0.1.0
- Architecture: x64 PE32+ host and embedded payload
- Runtime: static MSVC runtime; no VCRUNTIME/MSVCP dependency
- Embedded payload: byte-for-byte equal to the Release TTRHook64.dll
- Payload resource: ID 101, verified
- Public key resource: qualified ECDSA P-256 public blob, verified
- Embedded compatibility baseline: record 2620013101, byte-exact and signature verified
- Key provenance: $KeyDescription
- Icons: enabled, disabled, and warning groups; 16/20/24/32/48/256 px source entries
- Offline diagnostic from package-only directory: $(if ($supported) { 'supported' } else { 'valid baseline; unsupported runner identity; failed closed' })
- Live Explorer integration: qualified separately; package verification remained offline
- Compatibility data: qualified signed baseline embedded; no unqualified files bundled
- Private signing key: absent
- SHA-256: $hash
"@
[IO.File]::WriteAllText((Join-Path $output 'BUILD-REPORT.md'), $report,
  [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $source 'docs\MANUAL-QUALIFICATION.md') `
  -Destination (Join-Path $output 'MANUAL-QUALIFICATION.md')
