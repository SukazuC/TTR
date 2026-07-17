#include "crypto.h"
#include "manifest_selection.h"
#include "manifest_install.h"
#include "sha256.h"
#include "ttr_manifest.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <filesystem>
#include <fstream>
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

bool SaveSequence(const std::uint64_t sequence, void* context)
{
  *static_cast<std::uint64_t*>(context) = sequence;
  return true;
}

bool RejectSequence(std::uint64_t, void*)
{
  return false;
}

std::vector<std::byte> Read(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  file.seekg(0);
  if (size <= 0)
    return {};
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  return file ? bytes : std::vector<std::byte>{};
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

  const auto scratch = std::filesystem::temp_directory_path() /
                       (L"ttr-manifest-install-" + std::to_wstring(GetCurrentProcessId()));
  std::error_code filesystemError;
  std::filesystem::remove_all(scratch, filesystemError);
  std::uint64_t savedSequence = 0, installedSequence = 0;
  Check(ttr::InstallSignedManifestPair(pair.publicBlob, newer, newerSignature, scratch, 10,
                                       SaveSequence, &savedSequence, installedSequence, error) ==
            ttr::ManifestInstallResult::Installed &&
            savedSequence == 11 && installedSequence == 11 &&
            Read(scratch / L"compat.bin") == newer,
        "newer signed manifest installed and reloadable");
  const auto installedBytes = Read(scratch / L"compat.bin");
  Check(ttr::InstallSignedManifestPair(pair.publicBlob, newer, newerSignature, scratch, 11,
                                       SaveSequence, &savedSequence, installedSequence, error) ==
            ttr::ManifestInstallResult::NotNewer,
        "same manifest sequence rejected");
  Check(ttr::InstallSignedManifestPair(pair.publicBlob, older, olderSignature, scratch, 11,
                                       SaveSequence, &savedSequence, installedSequence, error) ==
            ttr::ManifestInstallResult::NotNewer,
        "older manifest sequence rejected");
  auto tamperedNewer = newer;
  tamperedNewer.back() ^= std::byte{1};
  Check(ttr::InstallSignedManifestPair(pair.publicBlob, tamperedNewer, newerSignature, scratch, 11,
                                       SaveSequence, &savedSequence, installedSequence, error) ==
            ttr::ManifestInstallResult::InvalidSignature &&
            Read(scratch / L"compat.bin") == installedBytes,
        "tampered download leaves installed manifest unchanged");
  auto tamperedNewerSignature = newerSignature;
  tamperedNewerSignature.front() ^= std::byte{1};
  Check(ttr::InstallSignedManifestPair(pair.publicBlob, newer, tamperedNewerSignature, scratch, 11,
                                       SaveSequence, &savedSequence, installedSequence, error) ==
            ttr::ManifestInstallResult::InvalidSignature &&
            Read(scratch / L"compat.bin") == installedBytes,
        "tampered signature leaves installed manifest unchanged");
  auto newest = Manifest(12);
  auto newestSignature = Sign(pair.key, newest);
  Check(ttr::InstallSignedManifestPair(pair.publicBlob, newest, newestSignature, scratch, 11,
                                       RejectSequence, nullptr, installedSequence, error) ==
            ttr::ManifestInstallResult::SequenceSaveFailure &&
            Read(scratch / L"compat.bin") == installedBytes,
        "failed highest-sequence save rolls installation back");
  Check(ttr::InstallSignedManifestPair(pair.publicBlob, older, olderSignature, scratch, 0,
                                       SaveSequence, &savedSequence, installedSequence, error) ==
            ttr::ManifestInstallResult::NotNewer,
        "installed external sequence cannot be rolled back");

  std::vector<ttr::ModuleIdentityV1> marker;
  std::array<ttr::ModuleIdentityV1, 1> identity{};
  identity[0].timeDateStamp = 1;
  Check(ttr::MarkManifestIdentityChecked(identity, marker),
        "unsupported identity starts automatic check once");
  Check(!ttr::MarkManifestIdentityChecked(identity, marker),
        "same unsupported identity does not repeat automatic check");
  identity[0].timeDateStamp = 2;
  Check(ttr::MarkManifestIdentityChecked(identity, marker),
        "new module identity permits one automatic check");
  Check(ttr::ShouldRetryCompatibilityAfterManifestReload(1, 2, true),
        "successful newer manifest reload causes compatibility retry when enabled");
  Check(!ttr::ShouldRetryCompatibilityAfterManifestReload(2, 2, true) &&
            !ttr::ShouldRetryCompatibilityAfterManifestReload(1, 2, false),
        "compatibility retry requires both a newer sequence and enabled state");
  std::filesystem::remove_all(scratch, filesystemError);
  return failures ? 1 : 0;
}
