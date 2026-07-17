#include "manifest_install.h"

#include "crypto.h"
#include "ttr_manifest.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace ttr
{
namespace
{
bool ReadBounded(const std::filesystem::path& path, const std::size_t limit,
                 std::vector<std::byte>& bytes)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return false;
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  file.seekg(0);
  if (size <= 0 || size > static_cast<std::streamoff>(limit))
    return false;
  bytes.resize(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  return !!file;
}

bool WriteFully(const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  std::size_t completed = 0;
  bool valid = true;
  while (completed < bytes.size())
  {
    const auto request =
        static_cast<DWORD>(std::min<std::size_t>(bytes.size() - completed, MAXDWORD));
    DWORD written = 0;
    if (!WriteFile(file, bytes.data() + completed, request, &written, nullptr) || !written)
    {
      valid = false;
      break;
    }
    completed += written;
  }
  valid = valid && FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  return valid;
}

bool ValidPair(const std::span<const std::byte> key, const std::filesystem::path& manifestPath,
               const std::filesystem::path& signaturePath, std::uint64_t& sequence)
{
  std::vector<std::byte> manifest, signature;
  ManifestView view;
  std::string error;
  if (!ReadBounded(manifestPath, kMaxManifestBytes, manifest) ||
      !ReadBounded(signaturePath, 256, signature) ||
      !VerifyEcdsaP256(key, manifest, signature, error) || !ParseManifest(manifest, view, error) ||
      !view.header->sequence)
    return false;
  sequence = view.header->sequence;
  return true;
}

void Restore(const std::filesystem::path& manifestPath, const std::filesystem::path& signaturePath,
             const std::filesystem::path& manifestBackup,
             const std::filesystem::path& signatureBackup, const bool hadPrior) noexcept
{
  if (hadPrior)
  {
    CopyFileW(manifestBackup.c_str(), manifestPath.c_str(), FALSE);
    CopyFileW(signatureBackup.c_str(), signaturePath.c_str(), FALSE);
  }
  else
  {
    DeleteFileW(manifestPath.c_str());
    DeleteFileW(signaturePath.c_str());
  }
}
} // namespace

ManifestInstallResult InstallSignedManifestPair(
    const std::span<const std::byte> publicKey, const std::span<const std::byte> manifest,
    const std::span<const std::byte> signature, const std::filesystem::path& directory,
    const std::uint64_t minimumSequence, const SaveManifestSequence saveSequence,
    void* const saveContext, std::uint64_t& installedSequence, std::string& error) noexcept
{
  installedSequence = 0;
  if (!VerifyEcdsaP256(publicKey, manifest, signature, error))
    return ManifestInstallResult::InvalidSignature;
  ManifestView candidate;
  if (!ParseManifest(manifest, candidate, error) || !candidate.header->sequence)
    return ManifestInstallResult::InvalidManifest;

  const auto manifestPath = directory / L"compat.bin";
  const auto signaturePath = directory / L"compat.sig";
  const auto manifestBackup = directory / L"compat.bin.bak";
  const auto signatureBackup = directory / L"compat.sig.bak";
  std::uint64_t floor = minimumSequence;
  std::uint64_t sequence = 0;
  if (ValidPair(publicKey, manifestPath, signaturePath, sequence))
    floor = std::max(floor, sequence);
  if (ValidPair(publicKey, manifestBackup, signatureBackup, sequence))
    floor = std::max(floor, sequence);
  if (candidate.header->sequence <= floor)
  {
    error = "downloaded manifest sequence is not newer";
    return ManifestInstallResult::NotNewer;
  }

  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec)
    return ManifestInstallResult::IoFailure;
  const auto manifestNew = directory / L"compat.bin.new";
  const auto signatureNew = directory / L"compat.sig.new";
  DeleteFileW(manifestNew.c_str());
  DeleteFileW(signatureNew.c_str());
  if (!WriteFully(manifestNew, manifest) || !WriteFully(signatureNew, signature))
  {
    DeleteFileW(manifestNew.c_str());
    DeleteFileW(signatureNew.c_str());
    return ManifestInstallResult::IoFailure;
  }

  const bool hadPrior = ValidPair(publicKey, manifestPath, signaturePath, sequence);
  if (hadPrior &&
      (!CopyFileW(manifestPath.c_str(), manifestBackup.c_str(), FALSE) ||
       !CopyFileW(signaturePath.c_str(), signatureBackup.c_str(), FALSE)))
  {
    DeleteFileW(manifestNew.c_str());
    DeleteFileW(signatureNew.c_str());
    return ManifestInstallResult::IoFailure;
  }
  const bool moved =
      MoveFileExW(manifestNew.c_str(), manifestPath.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) &&
      MoveFileExW(signatureNew.c_str(), signaturePath.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  std::uint64_t finalSequence = 0;
  if (!moved || !ValidPair(publicKey, manifestPath, signaturePath, finalSequence) ||
      finalSequence != candidate.header->sequence)
  {
    DeleteFileW(manifestNew.c_str());
    DeleteFileW(signatureNew.c_str());
    Restore(manifestPath, signaturePath, manifestBackup, signatureBackup, hadPrior);
    return ManifestInstallResult::IoFailure;
  }
  if (saveSequence && !saveSequence(finalSequence, saveContext))
  {
    Restore(manifestPath, signaturePath, manifestBackup, signatureBackup, hadPrior);
    return ManifestInstallResult::SequenceSaveFailure;
  }
  installedSequence = finalSequence;
  return ManifestInstallResult::Installed;
}
} // namespace ttr
