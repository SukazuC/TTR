#include "manifest_store.h"

#include "autostart.h"
#include "crypto.h"
#include "manifest_selection.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

namespace ttr::host
{
namespace
{

bool ReadBounded(const std::wstring& path, std::vector<std::byte>& output, const std::size_t limit)
{
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file)
    return false;
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  file.seekg(0);
  if (size <= 0 || size > static_cast<std::streamoff>(limit))
    return false;
  output.resize(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(output.data()), size);
  return !!file;
}

bool EmbeddedResource(const WORD id, std::span<const std::byte>& bytes) noexcept
{
  const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
  const auto loaded = resource ? LoadResource(nullptr, resource) : nullptr;
  const auto* data = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
  const DWORD size = resource ? SizeofResource(nullptr, resource) : 0;
  if (!data || !size)
    return false;
  bytes = {data, size};
  return true;
}

} // namespace

bool ManifestStore::Load(std::string& error) noexcept
{
  bytes_ = EmptyManifestBytes();
  sequence_ = 0;
  const auto base = ApplicationDataDirectory() + L"\\compat\\compat";
  std::span<const std::byte> publicKey, embeddedManifest, embeddedSignature;
  if (!EmbeddedResource(102, publicKey))
  {
    error = "embedded compatibility public key is missing";
    return false;
  }
  const bool hasEmbeddedManifest = EmbeddedResource(103, embeddedManifest);
  const bool hasEmbeddedSignature = EmbeddedResource(104, embeddedSignature);
  if (hasEmbeddedManifest != hasEmbeddedSignature)
  {
    error = "embedded compatibility baseline is incomplete";
    return false;
  }
  std::string keyError;
  if (!ValidateEcdsaP256PublicKey(publicKey, keyError) && !hasEmbeddedManifest)
    return ParseManifest(bytes_, view_, error);

  std::array<std::vector<std::byte>, 4> externalBytes;
  ReadBounded(base + L".bin", externalBytes[0], kMaxManifestBytes);
  ReadBounded(base + L".sig", externalBytes[1], 256);
  ReadBounded(base + L".bin.bak", externalBytes[2], kMaxManifestBytes);
  ReadBounded(base + L".sig.bak", externalBytes[3], 256);
  const std::array external{
      SignedManifestPair{externalBytes[0], externalBytes[1]},
      SignedManifestPair{externalBytes[2], externalBytes[3]},
  };
  ManifestSelectionResult selection;
  if (!SelectSignedManifest(publicKey, {embeddedManifest, embeddedSignature}, external, selection,
                            error))
    return false;
  bytes_ = std::move(selection.bytes);
  sequence_ = selection.sequence;
  return ParseManifest(bytes_, view_, error);
}

std::span<const std::byte> ManifestStore::SelectedRecordBlob(const std::vector<PeIdentity>& loaded,
                                                             std::uint64_t& id) const noexcept
{
  id = 0;
  std::vector<ModuleIdentityV1> identities;
  identities.reserve(loaded.size());
  for (const auto& image : loaded)
    identities.push_back(image.identity);
  bool ambiguous = false;
  const auto* record = SelectExactRecord(view_, identities, ambiguous);
  if (!record || ambiguous)
    return {};
  const auto expected = RecordModules(view_, *record);
  for (const auto& symbol : RecordSymbols(view_, *record))
  {
    const auto found = std::find_if(loaded.begin(), loaded.end(), [&](const PeIdentity& image) {
      return ModuleIdentityEqual(expected[symbol.moduleIndex], image.identity);
    });
    if (found == loaded.end() ||
        !ValidateSymbolRva(*found, symbol.rva, static_cast<SymbolKind>(symbol.kind)))
    {
      return {};
    }
  }
  id = record->recordId;
  return view_.bytes;
}

} // namespace ttr::host
