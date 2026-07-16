#pragma once

#include "ttr_manifest.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ttr
{

inline constexpr std::size_t kMaximumPeFileBytes = 512u * 1024u * 1024u;
inline constexpr std::uint16_t kMaximumPeSections = 96;

struct PeSection
{
  std::uint32_t virtualAddress{};
  std::uint32_t virtualSize{};
  std::uint32_t rawDataSize{};
  std::uint32_t rawDataPointer{};
  std::uint32_t characteristics{};
};

struct PeDebugEntry
{
  std::uint32_t type{};
  std::uint32_t sizeOfData{};
  std::uint32_t addressOfRawData{};
  std::uint32_t pointerToRawData{};
};

struct PeCodeViewIdentity
{
  GuidBytes guid{};
  std::uint32_t age{};
  std::string pdbPath;
};

struct PeImage
{
  std::string baseName;
  std::uint16_t machine{};
  std::uint16_t optionalHeaderMagic{};
  std::uint32_t timeDateStamp{};
  std::uint32_t sizeOfImage{};
  std::uint32_t sizeOfHeaders{};
  std::vector<PeSection> sections;
  std::vector<PeDebugEntry> debugEntries;
  std::optional<PeCodeViewIdentity> codeView;
};

struct PeIdentity
{
  ModuleIdentityV1 identity{};
  std::vector<PeSection> sections;
};

bool InspectPeImageBytes(std::span<const std::byte> bytes, std::string_view baseName, PeImage& out,
                         std::string& error) noexcept;
bool InspectPeImage(const std::wstring& path, PeImage& out, std::string& error) noexcept;
bool RequireCodeViewIdentity(const PeImage& image, PeIdentity& out, std::string& error) noexcept;
bool ReadPeIdentity(const std::wstring& path, PeIdentity& out, std::string& error) noexcept;
bool RvaToFileOffset(const PeImage& image, std::uint32_t rva, std::size_t size,
                     std::size_t fileSize, std::size_t& offset) noexcept;
bool ValidateSymbolRva(const PeIdentity&, std::uint32_t rva, SymbolKind kind) noexcept;
bool ValidateSymbolRva(const PeImage&, std::uint32_t rva, SymbolKind kind) noexcept;
std::wstring GuidString(const GuidBytes& guid);

} // namespace ttr
