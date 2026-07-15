#include "pe_identity.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <limits>
#include <objbase.h>

namespace ttr {
namespace {
bool ReadWholeFile(const std::wstring& path, std::vector<std::byte>& bytes) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{}; bool ok = false;
  if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= 512ll * 1024 * 1024) {
    bytes.resize(static_cast<std::size_t>(size.QuadPart)); DWORD read{};
    ok = ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
  }
  CloseHandle(f); return ok;
}
template<class T> const T* At(const std::vector<std::byte>& b, std::size_t o) {
  return CheckedRange(o, sizeof(T), b.size()) ? reinterpret_cast<const T*>(b.data() + o) : nullptr;
}
std::wstring BaseName(const std::wstring& path) {
  const auto p = path.find_last_of(L"\\/"); return p == std::wstring::npos ? path : path.substr(p + 1);
}
bool RvaToFile(std::uint32_t rva, const IMAGE_SECTION_HEADER* sections, std::uint16_t count,
               std::size_t fileSize, std::size_t& offset) {
  for (std::uint16_t i = 0; i < count; ++i) {
    const auto span = std::max(sections[i].Misc.VirtualSize, sections[i].SizeOfRawData);
    if (rva >= sections[i].VirtualAddress && rva - sections[i].VirtualAddress < span) {
      const auto delta = rva - sections[i].VirtualAddress;
      if (delta >= sections[i].SizeOfRawData) return false;
      offset = static_cast<std::size_t>(sections[i].PointerToRawData) + delta;
      return offset < fileSize;
    }
  }
  return false;
}
}

bool ReadPeIdentity(const std::wstring& path, PeIdentity& out, std::string& error) noexcept {
  out = {}; std::vector<std::byte> bytes;
  if (!ReadWholeFile(path, bytes)) { error = "unable to read file"; return false; }
  const auto* dos = At<IMAGE_DOS_HEADER>(bytes, 0);
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) { error = "bad DOS header"; return false; }
  const auto ntOffset = static_cast<std::size_t>(dos->e_lfanew);
  const auto* signature = At<DWORD>(bytes, ntOffset);
  const auto* file = At<IMAGE_FILE_HEADER>(bytes, ntOffset + sizeof(DWORD));
  if (!signature || *signature != IMAGE_NT_SIGNATURE || !file || file->Machine != IMAGE_FILE_MACHINE_AMD64) {
    error = "not an x64 PE image"; return false;
  }
  const auto optionalOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
  if (file->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
      !CheckedRange(optionalOffset, file->SizeOfOptionalHeader, bytes.size())) { error = "bad optional header"; return false; }
  const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(bytes.data() + optionalOffset);
  if (optional->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || optional->SizeOfImage == 0) {
    error = "bad PE32+ optional header"; return false;
  }
  const auto sectionOffset = optionalOffset + file->SizeOfOptionalHeader;
  const auto sectionBytes = static_cast<std::size_t>(file->NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
  if (!file->NumberOfSections || file->NumberOfSections > 96 ||
      !CheckedRange(sectionOffset, sectionBytes, bytes.size())) { error = "bad section table"; return false; }
  const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(bytes.data() + sectionOffset);
  auto name = BaseName(path);
  if (name.size() >= sizeof(out.identity.baseName)) { error = "module name too long"; return false; }
  for (std::size_t i = 0; i < name.size(); ++i) {
    const auto c = std::towlower(name[i]);
    if (c > 0x7f) { error = "non-ASCII module name"; return false; }
    out.identity.baseName[i] = static_cast<char>(c);
  }
  out.identity.timeDateStamp = file->TimeDateStamp;
  out.identity.sizeOfImage = optional->SizeOfImage;
  for (std::uint16_t i = 0; i < file->NumberOfSections; ++i)
    out.sections.push_back({sections[i].VirtualAddress, sections[i].Misc.VirtualSize, sections[i].Characteristics});

  if (optional->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) { error = "missing debug directory"; return false; }
  const auto& directory = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  std::size_t debugOffset{};
  if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_DEBUG_DIRECTORY) ||
      !RvaToFile(directory.VirtualAddress, sections, file->NumberOfSections, bytes.size(), debugOffset) ||
      !CheckedRange(debugOffset, directory.Size, bytes.size())) { error = "bad debug directory"; return false; }
  const auto count = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
  for (std::size_t i = 0; i < count; ++i) {
    const auto* debug = At<IMAGE_DEBUG_DIRECTORY>(bytes, debugOffset + i * sizeof(IMAGE_DEBUG_DIRECTORY));
    if (!debug || debug->Type != IMAGE_DEBUG_TYPE_CODEVIEW || debug->SizeOfData < 24 ||
        !CheckedRange(debug->PointerToRawData, debug->SizeOfData, bytes.size())) continue;
    const auto* cv = bytes.data() + debug->PointerToRawData;
    if (std::memcmp(cv, "RSDS", 4) != 0) continue;
    std::memcpy(out.identity.pdbGuid.value, cv + 4, 16);
    std::memcpy(&out.identity.pdbAge, cv + 20, 4);
    return true;
  }
  error = "RSDS CodeView identity not found"; return false;
}

bool ValidateSymbolRva(const PeIdentity& image, std::uint32_t rva, SymbolKind kind) noexcept {
  for (const auto& section : image.sections) {
    if (rva < section.virtualAddress || rva - section.virtualAddress >= section.virtualSize) continue;
    if (!(section.characteristics & IMAGE_SCN_MEM_READ)) return false;
    return kind == SymbolKind::Function ? !!(section.characteristics & IMAGE_SCN_MEM_EXECUTE)
                                        : !(section.characteristics & IMAGE_SCN_MEM_WRITE);
  }
  return false;
}

std::wstring GuidString(const GuidBytes& guid) {
  GUID value{}; std::memcpy(&value, guid.value, sizeof(value));
  wchar_t text[40]{};
  if (!StringFromGUID2(value, text, static_cast<int>(std::size(text)))) return {};
  return text;
}
}
