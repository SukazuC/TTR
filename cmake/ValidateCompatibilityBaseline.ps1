param(
  [Parameter(Mandatory = $true)][string]$PublicKey,
  [Parameter(Mandatory = $true)][string]$Manifest,
  [Parameter(Mandatory = $true)][string]$Signature
)

$ErrorActionPreference = 'Stop'
$keyBytes = [IO.File]::ReadAllBytes($PublicKey)
$manifestBytes = [IO.File]::ReadAllBytes($Manifest)
$signatureBytes = [IO.File]::ReadAllBytes($Signature)
if ($keyBytes.Length -ne 72 -or [BitConverter]::ToUInt32($keyBytes, 0) -ne 0x31534345 -or
    [BitConverter]::ToUInt32($keyBytes, 4) -ne 32) {
  throw 'public key is not a BCRYPT_ECDSA_PUBLIC_P256_MAGIC blob'
}
if ($signatureBytes.Length -ne 64) {
  throw 'baseline signature is not 64 bytes'
}
if ($manifestBytes.Length -lt 32 -or
    [Text.Encoding]::ASCII.GetString($manifestBytes, 0, 7) -ne 'TTRMAN2' -or
    [BitConverter]::ToUInt16($manifestBytes, 8) -ne 2 -or
    [BitConverter]::ToUInt16($manifestBytes, 10) -ne 32 -or
    [BitConverter]::ToUInt32($manifestBytes, 12) -ne $manifestBytes.Length -or
    [BitConverter]::ToUInt64($manifestBytes, 16) -eq 0) {
  throw 'baseline manifest header is malformed'
}
$parameters = [Security.Cryptography.ECParameters]::new()
$parameters.Curve = [Security.Cryptography.ECCurve+NamedCurves]::nistP256
$point = [Security.Cryptography.ECPoint]::new()
$point.X = $keyBytes[8..39]
$point.Y = $keyBytes[40..71]
$parameters.Q = $point
$ecdsa = [Security.Cryptography.ECDsa]::Create($parameters)
try {
  $valid = $ecdsa.VerifyData(
    $manifestBytes,
    $signatureBytes,
    [Security.Cryptography.HashAlgorithmName]::SHA256)
  if (-not $valid) {
    throw 'baseline signature does not match the public key'
  }
}
finally {
  $ecdsa.Dispose()
}
