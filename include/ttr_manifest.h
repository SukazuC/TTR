#pragma once
#include "ttr_symbol_ids.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ttr {
inline constexpr char kManifestMagic[8] = {'T','T','R','M','A','N','1','\0'};
inline constexpr std::uint16_t kManifestVersion = 1;
inline constexpr std::size_t kMaxManifestBytes = 1024 * 1024;

enum class SymbolKind : std::uint8_t { Function = 1, ReadOnlyData = 2 };
enum BackendFlags : std::uint32_t { BackendClassic = 1, BackendXaml = 2, BackendAnimatedXaml = 4 };

#pragma pack(push, 1)
struct GuidBytes { std::uint8_t value[16]; };
struct ModuleIdentityV1 {
  char baseName[40];
  std::uint32_t timeDateStamp;
  std::uint32_t sizeOfImage;
  GuidBytes pdbGuid;
  std::uint32_t pdbAge;
};
struct ManifestHeaderV1 {
  char magic[8];
  std::uint16_t formatVersion;
  std::uint16_t headerSize;
  std::uint32_t totalSize;
  std::uint64_t sequence;
  std::uint32_t recordCount;
  std::uint32_t recordTableOffset;
};
struct ManifestRecordV1 {
  std::uint64_t recordId;
  std::uint32_t backendFlags;
  std::uint32_t minimumProtocolVersion;
  std::uint32_t moduleCount;
  std::uint32_t moduleOffset;
  std::uint32_t symbolCount;
  std::uint32_t symbolOffset;
};
struct ManifestSymbolV1 {
  std::uint16_t symbolId;
  std::uint8_t moduleIndex;
  std::uint8_t kind;
  std::uint8_t required;
  std::uint8_t reserved[3];
  std::uint32_t rva;
};
#pragma pack(pop)

struct ManifestView {
  const ManifestHeaderV1* header{};
  std::span<const std::byte> bytes{};
  std::span<const ManifestRecordV1> records{};
};

bool CheckedRange(std::size_t offset, std::size_t size, std::size_t total) noexcept;
bool ParseManifest(std::span<const std::byte> bytes, ManifestView& out, std::string& error) noexcept;
std::span<const ModuleIdentityV1> RecordModules(const ManifestView&, const ManifestRecordV1&) noexcept;
std::span<const ManifestSymbolV1> RecordSymbols(const ManifestView&, const ManifestRecordV1&) noexcept;
bool ValidateRecord(const ManifestView&, const ManifestRecordV1&, std::string& error) noexcept;
}
