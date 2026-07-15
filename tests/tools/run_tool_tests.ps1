param(
  [Parameter(Mandatory = $true)][string]$Bin,
  [Parameter(Mandatory = $true)][string]$Source,
  [Parameter(Mandatory = $true)][string]$Scratch
)

$ErrorActionPreference = 'Continue'

function Invoke-Expected {
  param(
    [Parameter(Mandatory = $true)][string]$File,
    [Parameter(Mandatory = $true)][string[]]$Arguments,
    [Parameter(Mandatory = $true)][bool]$Success,
    [Parameter(Mandatory = $true)][string]$Name
  )
  & $File @Arguments 1>$null 2>$null
  $actual = $LASTEXITCODE -eq 0
  if ($actual -ne $Success) {
    throw "$Name returned exit code $LASTEXITCODE"
  }
}

$resolvedScratch = [IO.Path]::GetFullPath($Scratch)
$resolvedBin = [IO.Path]::GetFullPath($Bin)
if (-not $resolvedScratch.StartsWith($resolvedBin, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Tool-test scratch directory must remain inside the build configuration directory.'
}

if (Test-Path -LiteralPath $resolvedScratch) {
  Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedScratch | Out-Null

try {
  $moduleid = Join-Path $Bin 'moduleid.exe'
  $manifestc = Join-Path $Bin 'manifestc.exe'
  $manifestsign = Join-Path $Bin 'manifestsign.exe'
  $compatgen = Join-Path $Bin 'compatgen.exe'
  $fixture = Join-Path $Bin 'ttr_compat_fixture.dll'
  $fixturePdb = Join-Path $Bin 'ttr_compat_fixture.pdb'

  Invoke-Expected $moduleid @($fixture) $true 'moduleid exact success'
  Invoke-Expected $moduleid @('--inspect', $fixture) $true 'moduleid generic success'
  $badPe = Join-Path $resolvedScratch 'not-a-pe.bin'
  [IO.File]::WriteAllText($badPe, 'not a PE')
  Invoke-Expected $moduleid @($badPe) $false 'moduleid malformed failure'

  $manifestA = Join-Path $resolvedScratch 'compat-a.bin'
  $manifestB = Join-Path $resolvedScratch 'compat-b.bin'
  Invoke-Expected $manifestc @((Join-Path $Source 'empty.json'), $manifestA) $true 'manifestc valid JSON'
  Invoke-Expected $manifestc @((Join-Path $Source 'empty.json'), $manifestB) $true 'manifestc deterministic rerun'
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $manifestA).Hash -ne
      (Get-FileHash -Algorithm SHA256 -LiteralPath $manifestB).Hash) {
    throw 'manifestc output is not deterministic'
  }
  Invoke-Expected $manifestc @((Join-Path $Source 'malformed.json'),
                               (Join-Path $resolvedScratch 'bad.bin')) $false 'manifestc malformed JSON'

  $private = Join-Path $resolvedScratch 'ephemeral.private.blob'
  $public = Join-Path $resolvedScratch 'ephemeral.public.blob'
  Invoke-Expected $manifestsign @('generate', $private, $public) $true 'manifestsign generate'
  Invoke-Expected $manifestsign @('sign', $private, $manifestA) $true 'manifestsign sign'
  $signature = "$manifestA.sig"
  Invoke-Expected $manifestsign @('verify', $public, $manifestA, $signature) $true 'manifestsign verify'
  $tampered = Join-Path $resolvedScratch 'tampered.bin'
  [IO.File]::WriteAllBytes($tampered, [IO.File]::ReadAllBytes($manifestA))
  $tamperedBytes = [IO.File]::ReadAllBytes($tampered)
  $tamperedBytes[0] = $tamperedBytes[0] -bxor 1
  [IO.File]::WriteAllBytes($tampered, $tamperedBytes)
  Invoke-Expected $manifestsign @('verify', $public, $tampered, $signature) $false 'signature tamper rejection'

  $optionalOutput = Join-Path $resolvedScratch 'optional.json'
  Invoke-Expected $compatgen @($fixture, $fixturePdb, (Join-Path $Source 'optional.yaml'),
                               $optionalOutput) $true 'compatgen optional symbol'
  Invoke-Expected $compatgen @($fixture, $fixturePdb, (Join-Path $Source 'ambiguous.yaml'),
                               (Join-Path $resolvedScratch 'ambiguous.json')) $false 'compatgen ambiguity rejection'
  Invoke-Expected $compatgen @($fixture, $fixturePdb, (Join-Path $Source 'missing.yaml'),
                               (Join-Path $resolvedScratch 'missing.json')) $false 'compatgen missing symbol rejection'
  Invoke-Expected $compatgen @($fixture, (Join-Path $Bin 'moduleid.pdb'),
                               (Join-Path $Source 'optional.yaml'),
                               (Join-Path $resolvedScratch 'mismatch.json')) $false 'compatgen PDB mismatch rejection'
}
finally {
  if (Test-Path -LiteralPath $resolvedScratch) {
    Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
  }
}
