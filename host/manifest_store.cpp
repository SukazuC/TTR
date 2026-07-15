#include "manifest_store.h"

#include "autostart.h"
#include "crypto.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
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

bool EmbeddedPublicKey(std::span<const std::byte>& key) noexcept
{
  const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(102), RT_RCDATA);
  const auto loaded = resource ? LoadResource(nullptr, resource) : nullptr;
  const auto* data = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
  const DWORD size = resource ? SizeofResource(nullptr, resource) : 0;
  if (!data || !size)
    return false;
  key = {data, size};
  return true;
}

std::vector<std::byte> EmptyManifest()
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

} // namespace

bool ManifestStore::Load(std::string& error) noexcept
{
  bytes_ = EmptyManifest();
  const auto base = ApplicationDataDirectory() + L"\\compat\\compat";
  std::span<const std::byte> publicKey;
  if (EmbeddedPublicKey(publicKey))
  {
    auto tryPair = [&](const std::wstring& suffix) {
      std::vector<std::byte> candidate, signature;
      std::string verificationError;
      ManifestView candidateView;
      if (!ReadBounded(base + L".bin" + suffix, candidate, kMaxManifestBytes) ||
          !ReadBounded(base + L".sig" + suffix, signature, 256) ||
          !VerifyEcdsaP256(publicKey, candidate, signature, verificationError) ||
          !ParseManifest(candidate, candidateView, verificationError) ||
          candidateView.header->sequence == 0)
        return false;
      bytes_ = std::move(candidate);
      return true;
    };
    if (!tryPair(L""))
      tryPair(L".bak");
  }
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
