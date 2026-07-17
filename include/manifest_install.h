#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace ttr
{
enum class ManifestInstallResult
{
  Installed,
  NotNewer,
  InvalidSignature,
  InvalidManifest,
  IoFailure,
  SequenceSaveFailure,
};

using SaveManifestSequence = bool (*)(std::uint64_t sequence, void* context);

ManifestInstallResult InstallSignedManifestPair(
    std::span<const std::byte> publicKey, std::span<const std::byte> manifest,
    std::span<const std::byte> signature, const std::filesystem::path& directory,
    std::uint64_t minimumSequence, SaveManifestSequence saveSequence, void* saveContext,
    std::uint64_t& installedSequence, std::string& error) noexcept;
}
