#include "manifest_selection.h"

#include "crypto.h"
#include "ttr_manifest.h"

#include <cstring>

namespace ttr
{
std::vector<std::byte> EmptyManifestBytes()
{
  std::vector<std::byte> bytes(sizeof(ManifestHeaderV2));
  auto* header = reinterpret_cast<ManifestHeaderV2*>(bytes.data());
  std::memcpy(header->magic, kManifestMagic, sizeof(header->magic));
  header->formatVersion = kManifestVersion;
  header->headerSize = sizeof(*header);
  header->totalSize = static_cast<std::uint32_t>(bytes.size());
  header->recordTableOffset = sizeof(*header);
  return bytes;
}

bool SelectSignedManifest(const std::span<const std::byte> publicKey,
                          const SignedManifestPair embedded,
                          const std::span<const SignedManifestPair> external,
                          ManifestSelectionResult& result, std::string& error) noexcept
{
  result = {};
  if (!ValidateEcdsaP256PublicKey(publicKey, error))
    return false;

  std::uint64_t sequence{};
  result.embeddedPresent = !embedded.manifest.empty() || !embedded.signature.empty();
  if (result.embeddedPresent)
  {
    ManifestView view;
    if (embedded.manifest.empty() || embedded.signature.empty() ||
        !VerifyEcdsaP256(publicKey, embedded.manifest, embedded.signature, error) ||
        !ParseManifest(embedded.manifest, view, error) || !view.header->sequence)
    {
      error = "embedded compatibility baseline is invalid: " + error;
      return false;
    }
    result.embeddedSignatureValid = true;
    sequence = view.header->sequence;
    result.bytes.assign(embedded.manifest.begin(), embedded.manifest.end());
  }

  for (const auto& candidate : external)
  {
    ManifestView view;
    std::string candidateError;
    if (candidate.manifest.empty() || candidate.signature.empty() ||
        !VerifyEcdsaP256(publicKey, candidate.manifest, candidate.signature, candidateError) ||
        !ParseManifest(candidate.manifest, view, candidateError) || !view.header->sequence ||
        view.header->sequence <= sequence)
      continue;
    sequence = view.header->sequence;
    result.bytes.assign(candidate.manifest.begin(), candidate.manifest.end());
    result.externalSelected = true;
  }

  if (result.bytes.empty())
    result.bytes = EmptyManifestBytes();
  return true;
}
} // namespace ttr
