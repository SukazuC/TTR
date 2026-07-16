#include "crypto.h"
#include "manifest_selection.h"
#include "sha256.h"
#include "ttr_manifest.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
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

std::vector<std::byte> Manifest(const std::uint64_t sequence)
{
  auto bytes = ttr::EmptyManifestBytes();
  reinterpret_cast<ttr::ManifestHeaderV2*>(bytes.data())->sequence = sequence;
  return bytes;
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

  auto embedded = Manifest(10);
  auto embeddedSignature = Sign(pair.key, embedded);
  ttr::ManifestSelectionResult selection;
  Check(ttr::SelectSignedManifest(pair.publicBlob, {embedded, embeddedSignature}, {}, selection,
                                  error) &&
            selection.embeddedSignatureValid && selection.bytes == embedded,
        "valid embedded baseline selected");
  Check(ttr::SelectSignedManifest(pair.publicBlob, {}, {}, selection, error) &&
            selection.bytes.size() == sizeof(ttr::ManifestHeaderV2) && !selection.embeddedPresent,
        "missing embedded baseline remains fail closed");
  auto invalidEmbeddedSignature = embeddedSignature;
  invalidEmbeddedSignature[0] ^= std::byte{1};
  Check(!ttr::SelectSignedManifest(pair.publicBlob, {embedded, invalidEmbeddedSignature}, {},
                                   selection, error),
        "invalid embedded signature rejected");
  auto newer = Manifest(11);
  auto newerSignature = Sign(pair.key, newer);
  const std::array newerPair{ttr::SignedManifestPair{newer, newerSignature}};
  Check(ttr::SelectSignedManifest(pair.publicBlob, {embedded, embeddedSignature}, newerPair,
                                  selection, error) &&
            selection.externalSelected && selection.bytes == newer,
        "newer external manifest selected");
  auto older = Manifest(9);
  auto olderSignature = Sign(pair.key, older);
  const std::array olderPair{ttr::SignedManifestPair{older, olderSignature}};
  Check(ttr::SelectSignedManifest(pair.publicBlob, {embedded, embeddedSignature}, olderPair,
                                  selection, error) &&
            !selection.externalSelected && selection.bytes == embedded,
        "older external manifest cannot roll back baseline");
  auto invalidExternalSignature = newerSignature;
  invalidExternalSignature[0] ^= std::byte{1};
  const std::array invalidPair{ttr::SignedManifestPair{newer, invalidExternalSignature}};
  Check(ttr::SelectSignedManifest(pair.publicBlob, {embedded, embeddedSignature}, invalidPair,
                                  selection, error) &&
            !selection.externalSelected && selection.bytes == embedded,
        "invalid external pair falls back to embedded baseline");
  return failures ? 1 : 0;
}
