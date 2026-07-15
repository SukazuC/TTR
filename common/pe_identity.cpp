#include "pe_identity.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <limits>
#include <objbase.h>
#include <string_view>

namespace ttr
{
namespace
{

template <typename T>
const T* At(std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
  if (!CheckedRange(offset, sizeof(T), bytes.size()))
  {
    return nullptr;
  }
  return reinterpret_cast<const T*>(bytes.data() + offset);
}

bool CheckedAdd(const std::size_t left, const std::size_t right, std::size_t& result) noexcept
{
  if (right > std::numeric_limits<std::size_t>::max() - left)
  {
    return false;
  }
  result = left + right;
  return true;
}

bool CheckedMultiply(const std::size_t left, const std::size_t right, std::size_t& result) noexcept
{
  if (left && right > std::numeric_limits<std::size_t>::max() / left)
  {
    return false;
  }
  result = left * right;
  return true;
}

std::wstring BaseName(const std::wstring& path)
{
  const auto separator = path.find_last_of(L"\\/");
  return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

bool NarrowBaseName(const std::wstring& path, std::string& result)
{
  const auto name = BaseName(path);
  if (name.empty() || name.size() >= 40)
  {
    return false;
  }
  result.clear();
  result.reserve(name.size());
  for (const wchar_t character : name)
  {
    const auto lowered = std::towlower(character);
    if (lowered > 0x7f)
    {
      return false;
    }
    result.push_back(static_cast<char>(lowered));
  }
  return true;
}

bool ReadWholeFile(const std::wstring& path, std::vector<std::byte>& bytes,
                   std::string& error) noexcept
{
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
  {
    error = "unable to open file";
    return false;
  }

  LARGE_INTEGER size{};
  bool success = false;
  if (!GetFileSizeEx(file, &size))
  {
    error = "unable to determine file size";
  }
  else if (size.QuadPart <= 0)
  {
    error = "file is empty";
  }
  else if (static_cast<unsigned long long>(size.QuadPart) > kMaximumPeFileBytes)
  {
    error = "file exceeds the supported 512 MiB limit";
  }
  else
  {
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t completed = 0;
    success = true;
    while (completed < bytes.size())
    {
      const auto remaining = bytes.size() - completed;
      const DWORD requested =
          static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
      DWORD read = 0;
      if (!ReadFile(file, bytes.data() + completed, requested, &read, nullptr))
      {
        error = "unable to read file";
        success = false;
        break;
      }
      if (!read)
      {
        error = "file ended before the reported size";
        success = false;
        break;
      }
      completed += read;
    }
  }
  CloseHandle(file);
  return success;
}

bool SectionContainsRva(const PeSection& section, const std::uint32_t rva, const std::size_t size,
                        const bool requireRaw) noexcept
{
  const std::uint32_t span =
      requireRaw ? section.rawDataSize : std::max(section.virtualSize, section.rawDataSize);
  if (!span || rva < section.virtualAddress)
  {
    return false;
  }
  const auto delta = static_cast<std::uint64_t>(rva) - section.virtualAddress;
  return delta <= span && size <= static_cast<std::uint64_t>(span) - delta;
}

bool ValidateSection(const PeSection& section, const std::uint32_t sizeOfImage,
                     const std::size_t fileSize, std::string& error) noexcept
{
  const auto mappedSize = std::max(section.virtualSize, section.rawDataSize);
  if (mappedSize &&
      (section.virtualAddress >= sizeOfImage || mappedSize > sizeOfImage - section.virtualAddress))
  {
    error = "section virtual range exceeds SizeOfImage";
    return false;
  }
  if (section.rawDataSize && !CheckedRange(section.rawDataPointer, section.rawDataSize, fileSize))
  {
    error = "section raw-data range exceeds the file";
    return false;
  }
  return true;
}

bool ParseCodeView(std::span<const std::byte> bytes, const PeImage& image, PeImage& output,
                   std::string& error)
{
  std::optional<PeCodeViewIdentity> found;
  for (const auto& entry : image.debugEntries)
  {
    if (entry.type != IMAGE_DEBUG_TYPE_CODEVIEW)
    {
      continue;
    }
    if (entry.sizeOfData < 24)
    {
      error = "CodeView record is shorter than the RSDS GUID and age";
      return false;
    }
    if (!entry.pointerToRawData ||
        !CheckedRange(entry.pointerToRawData, entry.sizeOfData, bytes.size()))
    {
      error = "CodeView PointerToRawData range is outside the file";
      return false;
    }
    std::size_t mappedOffset = 0;
    if (!entry.addressOfRawData || !RvaToFileOffset(image, entry.addressOfRawData, entry.sizeOfData,
                                                    bytes.size(), mappedOffset))
    {
      error = "CodeView AddressOfRawData range is outside mapped section data";
      return false;
    }
    if (mappedOffset != entry.pointerToRawData)
    {
      error = "CodeView file offset and mapped-image RVA disagree";
      return false;
    }
    const auto* data = bytes.data() + entry.pointerToRawData;
    if (std::memcmp(data, "RSDS", 4) != 0)
    {
      continue;
    }
    if (found)
    {
      error = "multiple RSDS CodeView records are ambiguous";
      return false;
    }
    const auto pathBytes = entry.sizeOfData - 24u;
    const auto* path = reinterpret_cast<const char*>(data + 24);
    const auto* terminator = static_cast<const char*>(std::memchr(path, '\0', pathBytes));
    if (!terminator)
    {
      error = "RSDS PDB path is not NUL-terminated";
      return false;
    }
    PeCodeViewIdentity identity;
    std::memcpy(identity.guid.value, data + 4, sizeof(identity.guid.value));
    std::memcpy(&identity.age, data + 20, sizeof(identity.age));
    identity.pdbPath.assign(path, terminator);
    found = std::move(identity);
  }
  output.codeView = std::move(found);
  return true;
}

template <typename Image>
bool ValidateRva(const Image& image, const std::uint32_t rva, const SymbolKind kind) noexcept
{
  if (!rva || rva >= image.sizeOfImage)
  {
    return false;
  }
  for (const auto& section : image.sections)
  {
    if (!SectionContainsRva(section, rva, 1, false))
    {
      continue;
    }
    if (!(section.characteristics & IMAGE_SCN_MEM_READ))
    {
      return false;
    }
    if (kind == SymbolKind::Function)
    {
      return (section.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    }
    return (section.characteristics & IMAGE_SCN_MEM_WRITE) == 0;
  }
  return false;
}

} // namespace

bool RvaToFileOffset(const PeImage& image, const std::uint32_t rva, const std::size_t size,
                     const std::size_t fileSize, std::size_t& offset) noexcept
{
  offset = 0;
  for (const auto& section : image.sections)
  {
    if (!SectionContainsRva(section, rva, size, true))
    {
      continue;
    }
    const auto delta = static_cast<std::size_t>(rva - section.virtualAddress);
    std::size_t candidate = 0;
    if (!CheckedAdd(section.rawDataPointer, delta, candidate) ||
        !CheckedRange(candidate, size, fileSize))
    {
      return false;
    }
    offset = candidate;
    return true;
  }
  return false;
}

bool InspectPeImageBytes(const std::span<const std::byte> bytes, const std::string_view baseName,
                         PeImage& out, std::string& error) noexcept
{
  out = {};
  error.clear();
  if (bytes.size() > kMaximumPeFileBytes)
  {
    error = "file exceeds the supported 512 MiB limit";
    return false;
  }
  const auto* dos = At<IMAGE_DOS_HEADER>(bytes, 0);
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
  {
    error = "invalid DOS signature";
    return false;
  }
  if (dos->e_lfanew < 0)
  {
    error = "negative e_lfanew";
    return false;
  }
  const auto ntOffset = static_cast<std::size_t>(dos->e_lfanew);
  const auto* signature = At<DWORD>(bytes, ntOffset);
  if (!signature)
  {
    error = "e_lfanew points outside the file";
    return false;
  }
  if (*signature != IMAGE_NT_SIGNATURE)
  {
    error = "invalid NT signature";
    return false;
  }
  std::size_t fileHeaderOffset = 0;
  if (!CheckedAdd(ntOffset, sizeof(DWORD), fileHeaderOffset))
  {
    error = "NT-header offset overflow";
    return false;
  }
  const auto* file = At<IMAGE_FILE_HEADER>(bytes, fileHeaderOffset);
  if (!file)
  {
    error = "truncated COFF file header";
    return false;
  }
  if (file->Machine != IMAGE_FILE_MACHINE_AMD64)
  {
    error = "image machine is not x64";
    return false;
  }
  if (!file->NumberOfSections || file->NumberOfSections > kMaximumPeSections)
  {
    error = "section count is zero or unreasonable";
    return false;
  }
  std::size_t optionalOffset = 0;
  if (!CheckedAdd(fileHeaderOffset, sizeof(IMAGE_FILE_HEADER), optionalOffset) ||
      !CheckedRange(optionalOffset, file->SizeOfOptionalHeader, bytes.size()))
  {
    error = "truncated optional header";
    return false;
  }
  constexpr auto requiredOptionalBytes =
      offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) +
      (IMAGE_DIRECTORY_ENTRY_DEBUG + 1) * sizeof(IMAGE_DATA_DIRECTORY);
  if (file->SizeOfOptionalHeader < requiredOptionalBytes)
  {
    error = "optional header is too short for the debug data directory";
    return false;
  }
  const auto* optional =
      reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(bytes.data() + optionalOffset);
  if (optional->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
  {
    error = "optional header is not PE32+";
    return false;
  }
  if (!optional->SizeOfImage || !optional->SizeOfHeaders ||
      optional->SizeOfHeaders > bytes.size() ||
      optional->NumberOfRvaAndSizes > IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
  {
    error = "optional-header image bounds are invalid";
    return false;
  }
  std::size_t sectionOffset = 0;
  std::size_t sectionBytes = 0;
  if (!CheckedAdd(optionalOffset, file->SizeOfOptionalHeader, sectionOffset) ||
      !CheckedMultiply(file->NumberOfSections, sizeof(IMAGE_SECTION_HEADER), sectionBytes) ||
      !CheckedRange(sectionOffset, sectionBytes, bytes.size()))
  {
    error = "section table is truncated or overflows";
    return false;
  }

  out.baseName.assign(baseName);
  out.machine = file->Machine;
  out.optionalHeaderMagic = optional->Magic;
  out.timeDateStamp = file->TimeDateStamp;
  out.sizeOfImage = optional->SizeOfImage;
  out.sizeOfHeaders = optional->SizeOfHeaders;
  const auto* sections =
      reinterpret_cast<const IMAGE_SECTION_HEADER*>(bytes.data() + sectionOffset);
  out.sections.reserve(file->NumberOfSections);
  for (std::uint16_t index = 0; index < file->NumberOfSections; ++index)
  {
    PeSection section{sections[index].VirtualAddress, sections[index].Misc.VirtualSize,
                      sections[index].SizeOfRawData, sections[index].PointerToRawData,
                      sections[index].Characteristics};
    if (!ValidateSection(section, out.sizeOfImage, bytes.size(), error))
    {
      out = {};
      return false;
    }
    out.sections.push_back(section);
  }

  if (optional->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
  {
    return true;
  }
  const auto& directory = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  if (!directory.VirtualAddress && !directory.Size)
  {
    return true;
  }
  if (!directory.VirtualAddress || !directory.Size ||
      directory.Size % sizeof(IMAGE_DEBUG_DIRECTORY) != 0)
  {
    error = "debug directory has an invalid RVA or size";
    out = {};
    return false;
  }
  std::size_t debugOffset = 0;
  if (!RvaToFileOffset(out, directory.VirtualAddress, directory.Size, bytes.size(), debugOffset))
  {
    error = "debug-directory RVA range is outside section raw data";
    out = {};
    return false;
  }
  const auto count = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
  out.debugEntries.reserve(count);
  for (std::size_t index = 0; index < count; ++index)
  {
    const auto* entry =
        At<IMAGE_DEBUG_DIRECTORY>(bytes, debugOffset + index * sizeof(IMAGE_DEBUG_DIRECTORY));
    if (!entry)
    {
      error = "debug-directory entry is truncated";
      out = {};
      return false;
    }
    out.debugEntries.push_back(
        {entry->Type, entry->SizeOfData, entry->AddressOfRawData, entry->PointerToRawData});
  }
  PeImage parsed = out;
  if (!ParseCodeView(bytes, parsed, out, error))
  {
    out = {};
    return false;
  }
  return true;
}

bool InspectPeImage(const std::wstring& path, PeImage& out, std::string& error) noexcept
{
  std::vector<std::byte> bytes;
  if (!ReadWholeFile(path, bytes, error))
  {
    out = {};
    return false;
  }
  std::string name;
  if (!NarrowBaseName(path, name))
  {
    out = {};
    error = "module base name must be non-empty ASCII and shorter than 40 bytes";
    return false;
  }
  return InspectPeImageBytes(bytes, name, out, error);
}

bool RequireCodeViewIdentity(const PeImage& image, PeIdentity& out, std::string& error) noexcept
{
  out = {};
  if (!image.codeView)
  {
    error = "RSDS CodeView identity not found";
    return false;
  }
  if (image.baseName.empty() || image.baseName.size() >= sizeof(out.identity.baseName))
  {
    error = "module base name is invalid";
    return false;
  }
  std::memcpy(out.identity.baseName, image.baseName.data(), image.baseName.size());
  out.identity.timeDateStamp = image.timeDateStamp;
  out.identity.sizeOfImage = image.sizeOfImage;
  out.identity.pdbGuid = image.codeView->guid;
  out.identity.pdbAge = image.codeView->age;
  out.sections = image.sections;
  return true;
}

bool ReadPeIdentity(const std::wstring& path, PeIdentity& out, std::string& error) noexcept
{
  PeImage image;
  if (!InspectPeImage(path, image, error))
  {
    out = {};
    return false;
  }
  return RequireCodeViewIdentity(image, out, error);
}

bool ValidateSymbolRva(const PeIdentity& image, const std::uint32_t rva,
                       const SymbolKind kind) noexcept
{
  PeImage generic;
  generic.sizeOfImage = image.identity.sizeOfImage;
  generic.sections = image.sections;
  return ValidateRva(generic, rva, kind);
}

bool ValidateSymbolRva(const PeImage& image, const std::uint32_t rva,
                       const SymbolKind kind) noexcept
{
  return ValidateRva(image, rva, kind);
}

std::wstring GuidString(const GuidBytes& guid)
{
  GUID value{};
  std::memcpy(&value, guid.value, sizeof(value));
  wchar_t text[40]{};
  if (!StringFromGUID2(value, text, static_cast<int>(std::size(text))))
  {
    return {};
  }
  return text;
}

} // namespace ttr
