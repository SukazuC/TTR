#pragma once
#include "ttr_manifest.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ttr
{
struct SignedManifestPair
{
  std::span<const std::byte> manifest;
  std::span<const std::byte> signature;
};

struct ManifestSelectionResult
{
  std::vector<std::byte> bytes;
  std::uint64_t sequence{};
  bool embeddedPresent{};
  bool embeddedSignatureValid{};
  bool externalSelected{};
};

std::vector<std::byte> EmptyManifestBytes();
bool SelectSignedManifest(std::span<const std::byte> publicKey, SignedManifestPair embedded,
                          std::span<const SignedManifestPair> external,
                          ManifestSelectionResult& result, std::string& error) noexcept;
bool MarkManifestIdentityChecked(std::span<const ModuleIdentityV1> identities,
                                 std::vector<ModuleIdentityV1>& marker) noexcept;
bool ShouldRetryCompatibilityAfterManifestReload(std::uint64_t previousSequence,
                                                 std::uint64_t currentSequence,
                                                 bool enabled) noexcept;
} // namespace ttr
