#include "crypto.h"
#include "sha256.h"

#include <Windows.h>
#include <bcrypt.h>

#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void Check(const bool value, const char* name)
{
  if (!value)
  {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

struct KeyPair
{
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_KEY_HANDLE key{};
  std::vector<std::byte> publicBlob;

  ~KeyPair()
  {
    if (key)
      BCryptDestroyKey(key);
    if (algorithm)
      BCryptCloseAlgorithmProvider(algorithm, 0);
  }
};

bool Generate(KeyPair& pair)
{
  if (BCryptOpenAlgorithmProvider(&pair.algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptGenerateKeyPair(pair.algorithm, &pair.key, 256, 0) < 0 ||
      BCryptFinalizeKeyPair(pair.key, 0) < 0)
  {
    return false;
  }
  ULONG size = 0;
  if (BCryptExportKey(pair.key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &size, 0) < 0)
  {
    return false;
  }
  pair.publicBlob.resize(size);
  return BCryptExportKey(pair.key, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                         reinterpret_cast<PUCHAR>(pair.publicBlob.data()), size, &size, 0) >= 0;
}

std::vector<std::byte> Sign(BCRYPT_KEY_HANDLE key, const std::span<const std::byte> data)
{
  ttr::Sha256 digest{};
  if (!ttr::Sha256Bytes(data, digest))
    return {};
  ULONG size = 0;
  if (BCryptSignHash(key, nullptr, reinterpret_cast<PUCHAR>(digest.data()),
                     static_cast<ULONG>(digest.size()), nullptr, 0, &size, 0) < 0)
  {
    return {};
  }
  std::vector<std::byte> signature(size);
  if (BCryptSignHash(key, nullptr, reinterpret_cast<PUCHAR>(digest.data()),
                     static_cast<ULONG>(digest.size()), reinterpret_cast<PUCHAR>(signature.data()),
                     size, &size, 0) < 0)
  {
    return {};
  }
  return signature;
}

} // namespace

int main()
{
  KeyPair pair;
  Check(Generate(pair), "ephemeral P-256 key generation");
  std::vector<std::byte> manifest{std::byte{1}, std::byte{2}, std::byte{3}};
  auto signature = Sign(pair.key, manifest);
  Check(signature.size() == ttr::kEcdsaP256SignatureBytes,
        "ECDSA signing produces 64-byte signature");
  std::string error;
  Check(ttr::ValidateEcdsaP256PublicKey(pair.publicBlob, error), "generated public key validates");
  Check(ttr::VerifyEcdsaP256(pair.publicBlob, manifest, signature, error),
        "valid signature verifies");
  auto modifiedManifest = manifest;
  modifiedManifest[0] ^= std::byte{1};
  Check(!ttr::VerifyEcdsaP256(pair.publicBlob, modifiedManifest, signature, error),
        "modified manifest rejected");
  auto modifiedSignature = signature;
  modifiedSignature[0] ^= std::byte{1};
  Check(!ttr::VerifyEcdsaP256(pair.publicBlob, manifest, modifiedSignature, error),
        "modified signature rejected");
  KeyPair wrong;
  Check(Generate(wrong), "second ephemeral key generation");
  Check(!ttr::VerifyEcdsaP256(wrong.publicBlob, manifest, signature, error),
        "wrong public key rejected");
  signature.pop_back();
  Check(!ttr::VerifyEcdsaP256(pair.publicBlob, manifest, signature, error),
        "truncated signature rejected");
  const std::vector<std::byte> invalidKey(8);
  Check(!ttr::ValidateEcdsaP256PublicKey(invalidKey, error), "invalid key blob rejected");
  return failures ? 1 : 0;
}
