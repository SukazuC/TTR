#include "pe_identity.h"

#include <Windows.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void Check(const bool value, const char* name)
{
  if (!value)
  {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

template <typename T> T* At(std::vector<std::byte>& bytes, const std::size_t offset)
{
  return reinterpret_cast<T*>(bytes.data() + offset);
}

std::vector<std::byte> Fixture()
{
  std::vector<std::byte> bytes(0x800);
  auto* dos = At<IMAGE_DOS_HEADER>(bytes, 0);
  dos->e_magic = IMAGE_DOS_SIGNATURE;
  dos->e_lfanew = 0x80;
  *At<DWORD>(bytes, 0x80) = IMAGE_NT_SIGNATURE;
  auto* file = At<IMAGE_FILE_HEADER>(bytes, 0x84);
  file->Machine = IMAGE_FILE_MACHINE_AMD64;
  file->NumberOfSections = 2;
  file->TimeDateStamp = 0x12345678;
  file->SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
  auto* optional = At<IMAGE_OPTIONAL_HEADER64>(bytes, 0x98);
  optional->Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  optional->SectionAlignment = 0x1000;
  optional->FileAlignment = 0x200;
  optional->SizeOfImage = 0x4000;
  optional->SizeOfHeaders = 0x200;
  optional->NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
  optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG] = {0x2000, sizeof(IMAGE_DEBUG_DIRECTORY)};
  auto* sections = At<IMAGE_SECTION_HEADER>(bytes, 0x188);
  std::memcpy(sections[0].Name, ".text", 5);
  sections[0].Misc.VirtualSize = 0x100;
  sections[0].VirtualAddress = 0x1000;
  sections[0].SizeOfRawData = 0x200;
  sections[0].PointerToRawData = 0x200;
  sections[0].Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
  std::memcpy(sections[1].Name, ".rdata", 6);
  sections[1].Misc.VirtualSize = 0x400;
  sections[1].VirtualAddress = 0x2000;
  sections[1].SizeOfRawData = 0x400;
  sections[1].PointerToRawData = 0x400;
  sections[1].Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
  auto* debug = At<IMAGE_DEBUG_DIRECTORY>(bytes, 0x400);
  debug->Type = IMAGE_DEBUG_TYPE_CODEVIEW;
  debug->SizeOfData = 32;
  debug->AddressOfRawData = 0x2100;
  debug->PointerToRawData = 0x500;
  auto* codeView = bytes.data() + 0x500;
  std::memcpy(codeView, "RSDS", 4);
  for (std::size_t index = 0; index < 16; ++index)
  {
    codeView[4 + index] = static_cast<std::byte>(index + 1);
  }
  const std::uint32_t age = 7;
  std::memcpy(codeView + 20, &age, sizeof(age));
  std::memcpy(codeView + 24, "a.pdb\0", 6);
  return bytes;
}

bool Inspect(const std::vector<std::byte>& bytes, ttr::PeImage& image, std::string& error)
{
  return ttr::InspectPeImageBytes(bytes, "fixture.dll", image, error);
}

void PeCases()
{
  ttr::PeImage image;
  ttr::PeIdentity identity;
  std::string error;
  const auto valid = Fixture();
  Check(Inspect(valid, image, error), "valid x64 PE with RSDS");
  Check(image.machine == IMAGE_FILE_MACHINE_AMD64 && image.codeView.has_value(),
        "generic PE metadata");
  Check(ttr::RequireCodeViewIdentity(image, identity, error) && identity.identity.pdbAge == 7,
        "strict CodeView identity");

  auto bytes = valid;
  At<IMAGE_OPTIONAL_HEADER64>(bytes, 0x98)->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG] = {};
  Check(Inspect(bytes, image, error) && !image.codeView, "valid PE without debug directory");
  Check(!ttr::RequireCodeViewIdentity(image, identity, error),
        "strict identity rejects missing debug directory");

  bytes = valid;
  At<IMAGE_DEBUG_DIRECTORY>(bytes, 0x400)->Type = IMAGE_DEBUG_TYPE_MISC;
  At<IMAGE_DEBUG_DIRECTORY>(bytes, 0x400)->SizeOfData = 0;
  Check(Inspect(bytes, image, error) && !image.codeView, "debug directory without RSDS");

  bytes = valid;
  At<IMAGE_DOS_HEADER>(bytes, 0)->e_magic = 0;
  Check(!Inspect(bytes, image, error), "invalid DOS signature");
  bytes = valid;
  At<IMAGE_DOS_HEADER>(bytes, 0)->e_lfanew = -1;
  Check(!Inspect(bytes, image, error), "negative e_lfanew");
  bytes = valid;
  At<IMAGE_DOS_HEADER>(bytes, 0)->e_lfanew = 0x900;
  Check(!Inspect(bytes, image, error), "e_lfanew outside file");
  bytes = valid;
  *At<DWORD>(bytes, 0x80) = 0;
  Check(!Inspect(bytes, image, error), "invalid NT signature");
  bytes = valid;
  At<IMAGE_FILE_HEADER>(bytes, 0x84)->Machine = IMAGE_FILE_MACHINE_I386;
  Check(!Inspect(bytes, image, error), "x86 rejected");
  bytes = valid;
  At<IMAGE_FILE_HEADER>(bytes, 0x84)->SizeOfOptionalHeader = 16;
  Check(!Inspect(bytes, image, error), "truncated optional header");
  bytes = valid;
  At<IMAGE_FILE_HEADER>(bytes, 0x84)->NumberOfSections = 96;
  Check(!Inspect(bytes, image, error), "invalid section table");
  bytes = valid;
  At<IMAGE_OPTIONAL_HEADER64>(bytes, 0x98)
      ->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
      .VirtualAddress = 0x3000;
  Check(!Inspect(bytes, image, error), "invalid debug directory RVA");
  bytes = valid;
  At<IMAGE_OPTIONAL_HEADER64>(bytes, 0x98)->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG] = {
      0x23f0, sizeof(IMAGE_DEBUG_DIRECTORY)};
  Check(!Inspect(bytes, image, error), "invalid debug directory file range");
  bytes = valid;
  At<IMAGE_DEBUG_DIRECTORY>(bytes, 0x400)->PointerToRawData = 0x900;
  Check(!Inspect(bytes, image, error), "invalid CodeView file offset");
  bytes = valid;
  At<IMAGE_DEBUG_DIRECTORY>(bytes, 0x400)->SizeOfData = 23;
  Check(!Inspect(bytes, image, error), "truncated CodeView identity");
  bytes = valid;
  std::memcpy(bytes.data() + 0x500, "NB10", 4);
  Check(Inspect(bytes, image, error) && !image.codeView, "non-RSDS CodeView format");
  bytes = valid;
  auto* optional = At<IMAGE_OPTIONAL_HEADER64>(bytes, 0x98);
  optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size = 2 * sizeof(IMAGE_DEBUG_DIRECTORY);
  auto* second = At<IMAGE_DEBUG_DIRECTORY>(bytes, 0x41c);
  *second = *At<IMAGE_DEBUG_DIRECTORY>(bytes, 0x400);
  second->AddressOfRawData = 0x2140;
  second->PointerToRawData = 0x540;
  std::memcpy(bytes.data() + 0x540, bytes.data() + 0x500, 32);
  Check(!Inspect(bytes, image, error), "multiple RSDS records");
  bytes = valid;
  At<IMAGE_SECTION_HEADER>(bytes, 0x188)[1].PointerToRawData = 0xfffffff0u;
  Check(!Inspect(bytes, image, error), "section file-range integer overflow");
}

void RvaCases()
{
  auto bytes = Fixture();
  ttr::PeImage image;
  std::string error;
  Check(Inspect(bytes, image, error), "RVA fixture parses");
  std::size_t offset = 0;
  Check(ttr::RvaToFileOffset(image, 0x1000, 1, bytes.size(), offset) && offset == 0x200,
        "RVA first byte boundary");
  Check(ttr::RvaToFileOffset(image, 0x11ff, 1, bytes.size(), offset) && offset == 0x3ff,
        "RVA last raw byte boundary");
  Check(!ttr::RvaToFileOffset(image, 0x1200, 1, bytes.size(), offset), "RVA one past raw boundary");
  Check(!ttr::RvaToFileOffset(image, 0x100, 1, bytes.size(), offset),
        "header RVA rejected as section data");

  ttr::PeImage permissions;
  permissions.sizeOfImage = 0x5000;
  permissions.sections = {{0x1000, 0x100, 0x100, 0, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE},
                          {0x2000, 0x100, 0x100, 0, IMAGE_SCN_MEM_READ},
                          {0x3000, 0x100, 0x100, 0, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE},
                          {0x4000, 0, 0, 0, IMAGE_SCN_MEM_READ}};
  Check(ttr::ValidateSymbolRva(permissions, 0x1000, ttr::SymbolKind::Function),
        "executable symbol RVA");
  Check(ttr::ValidateSymbolRva(permissions, 0x2000, ttr::SymbolKind::ReadOnlyData),
        "read-only symbol RVA");
  Check(!ttr::ValidateSymbolRva(permissions, 0x3000, ttr::SymbolKind::ReadOnlyData),
        "writable symbol RVA rejected");
  Check(!ttr::ValidateSymbolRva(permissions, 0x5000, ttr::SymbolKind::Function),
        "out-of-range symbol RVA rejected");
  Check(!ttr::ValidateSymbolRva(permissions, 0x4000, ttr::SymbolKind::ReadOnlyData),
        "zero-length section rejected");
}

} // namespace

int main()
{
  PeCases();
  RvaCases();
  return failures ? 1 : 0;
}
