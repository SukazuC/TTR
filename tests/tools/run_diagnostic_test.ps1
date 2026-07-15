param(
  [Parameter(Mandatory = $true)][string]$HostExe,
  [Parameter(Mandatory = $true)][int]$DevelopmentKey,
  [Parameter(Mandatory = $true)][string]$Scratch
)

$ErrorActionPreference = 'Stop'
$scratchPath = [IO.Path]::GetFullPath($Scratch)
$hostDirectory = [IO.Path]::GetFullPath((Split-Path -Parent $HostExe))
if (-not $scratchPath.StartsWith($hostDirectory, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Diagnostic scratch path must remain inside the build configuration directory.'
}
New-Item -ItemType Directory -Force -Path $scratchPath | Out-Null
$stdout = Join-Path $scratchPath 'stdout.json'
$stderr = Join-Path $scratchPath 'stderr.txt'
try {
  $process = Start-Process -FilePath $HostExe -ArgumentList @('--diagnose-offline', '--json') `
    -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $expectedExit = if ($DevelopmentKey) { 4 } else { 0 }
  if ($process.ExitCode -ne $expectedExit) {
    throw "offline diagnostic exit $($process.ExitCode), expected $expectedExit"
  }
  $report = Get-Content -Raw -LiteralPath $stdout | ConvertFrom-Json
  if ($report.live_integration -ne $false -or $report.payload_valid -ne $true -or
      $report.modules.Count -lt 1) {
    throw 'offline diagnostic report failed its safety or payload assertions'
  }
  if ($DevelopmentKey -and $report.public_key_valid -ne $false) {
    throw 'placeholder public key did not fail closed'
  }
  if (-not $DevelopmentKey -and $report.public_key_valid -ne $true) {
    throw 'personalized public key did not validate'
  }
}
finally {
  Remove-Item -LiteralPath $scratchPath -Recurse -Force -ErrorAction SilentlyContinue
}
