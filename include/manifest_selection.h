#pragma once

#include <cstddef>
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
  bool embeddedPresent{};
  bool embeddedSignatureValid{};
  bool externalSelected{};
};

std::vector<std::byte> EmptyManifestBytes();
bool SelectSignedManifest(std::span<const std::byte> publicKey, SignedManifestPair embedded,
                          std::span<const SignedManifestPair> external,
                          ManifestSelectionResult& result, std::string& error) noexcept;
} // namespace ttr
