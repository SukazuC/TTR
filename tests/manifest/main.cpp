#include "ttr_manifest.h"

#include <algorithm>
#include <cstring>
#include <iostream>
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

template <typename T> void Append(std::vector<std::byte>& bytes, const T& value)
{
  const auto* begin = reinterpret_cast<const std::byte*>(&value);
  bytes.insert(bytes.end(), begin, begin + sizeof(value));
}

ttr::ModuleIdentityV1 Module(const char* name = "taskbar.dll")
{
  ttr::ModuleIdentityV1 module{};
  strcpy_s(module.baseName, name);
  module.timeDateStamp = 123;
  module.sizeOfImage = 0x10000;
  module.pdbAge = 1;
  module.pdbGuid.value[0] = 1;
  return module;
}

std::vector<ttr::ManifestSymbolV1> ClassicSymbols()
{
  using S = ttr::SymbolId;
  const S ids[] = {S::TaskListWnd_Vtable_ITaskListUI,
                   S::TaskListThumbnailWnd_GetHoverIndex,
                   S::TaskListThumbnailWnd_GetTaskItem,
                   S::TaskListThumbnailWnd_GetTaskGroup,
                   S::TaskListThumbnailWnd_TaskReordered,
                   S::TaskListThumbnailWnd_WndProc,
                   S::TaskGroup_GetNumItems,
                   S::TaskListWnd_GetTBGroupFromGroup,
                   S::TaskBtnGroup_GetGroupType,
                   S::TaskBtnGroup_IndexOfTaskItem,
                   S::TaskListWnd_TaskInclusionChanged,
                   S::TaskItemFilter_IsTaskAllowed};
  std::vector<ttr::ManifestSymbolV1> symbols;
  for (std::size_t index = 0; index < std::size(ids); ++index)
  {
    ttr::ManifestSymbolV1 symbol{};
    symbol.symbolId = static_cast<std::uint16_t>(ids[index]);
    symbol.moduleIndex = 0;
    symbol.kind = static_cast<std::uint8_t>(ids[index] == S::TaskListWnd_Vtable_ITaskListUI
                                                ? ttr::SymbolKind::ReadOnlyData
                                                : ttr::SymbolKind::Function);
    symbol.required = 1;
    symbol.rva = 0x1000 + static_cast<std::uint32_t>(index * 16);
    symbols.push_back(symbol);
  }
  return symbols;
}

std::vector<std::byte> EmptyManifest()
{
  std::vector<std::byte> bytes(sizeof(ttr::ManifestHeaderV1));
  auto* header = reinterpret_cast<ttr::ManifestHeaderV1*>(bytes.data());
  std::memcpy(header->magic, ttr::kManifestMagic, sizeof(header->magic));
  header->formatVersion = ttr::kManifestVersion;
  header->headerSize = sizeof(*header);
  header->totalSize = static_cast<std::uint32_t>(bytes.size());
  header->recordTableOffset = sizeof(*header);
  return bytes;
}

std::vector<std::byte> OneRecord()
{
  const auto module = Module();
  const auto symbols = ClassicSymbols();
  std::vector<std::byte> bytes(sizeof(ttr::ManifestHeaderV1) + sizeof(ttr::ManifestRecordV1));
  ttr::ManifestRecordV1 record{};
  record.recordId = 42;
  record.backendFlags = ttr::BackendClassic;
  record.minimumProtocolVersion = 1;
  record.moduleCount = 1;
  record.moduleOffset = static_cast<std::uint32_t>(bytes.size());
  Append(bytes, module);
  record.symbolCount = static_cast<std::uint32_t>(symbols.size());
  record.symbolOffset = static_cast<std::uint32_t>(bytes.size());
  for (const auto& symbol : symbols)
  {
    Append(bytes, symbol);
  }
  auto* header = reinterpret_cast<ttr::ManifestHeaderV1*>(bytes.data());
  std::memcpy(header->magic, ttr::kManifestMagic, sizeof(header->magic));
  header->formatVersion = ttr::kManifestVersion;
  header->headerSize = sizeof(*header);
  header->totalSize = static_cast<std::uint32_t>(bytes.size());
  header->sequence = 7;
  header->recordCount = 1;
  header->recordTableOffset = sizeof(*header);
  std::memcpy(bytes.data() + header->recordTableOffset, &record, sizeof(record));
  return bytes;
}

ttr::ManifestRecordV1* Record(std::vector<std::byte>& bytes)
{
  auto* header = reinterpret_cast<ttr::ManifestHeaderV1*>(bytes.data());
  return reinterpret_cast<ttr::ManifestRecordV1*>(bytes.data() + header->recordTableOffset);
}

ttr::ManifestSymbolV1* FirstSymbol(std::vector<std::byte>& bytes)
{
  return reinterpret_cast<ttr::ManifestSymbolV1*>(bytes.data() + Record(bytes)->symbolOffset);
}

void ParseCases()
{
  ttr::ManifestView view;
  std::string error;
  auto bytes = EmptyManifest();
  Check(ttr::ParseManifest(bytes, view, error), "valid empty manifest");
  bytes[0] = std::byte{'X'};
  Check(!ttr::ParseManifest(bytes, view, error), "malformed magic");
  bytes = EmptyManifest();
  reinterpret_cast<ttr::ManifestHeaderV1*>(bytes.data())->totalSize++;
  Check(!ttr::ParseManifest(bytes, view, error), "malformed total size");
  bytes = EmptyManifest();
  auto* header = reinterpret_cast<ttr::ManifestHeaderV1*>(bytes.data());
  header->recordCount = UINT32_MAX;
  Check(!ttr::ParseManifest(bytes, view, error), "excessive record count");
  bytes = EmptyManifest();
  header = reinterpret_cast<ttr::ManifestHeaderV1*>(bytes.data());
  header->recordCount = 1;
  header->recordTableOffset = UINT32_MAX;
  Check(!ttr::ParseManifest(bytes, view, error), "record offset overflow");

  const auto deterministicA = OneRecord();
  const auto deterministicB = OneRecord();
  Check(deterministicA == deterministicB, "deterministic serialization fixture");
  bytes = deterministicA;
  Check(ttr::ParseManifest(bytes, view, error), "valid classic record");
  bytes = deterministicA;
  FirstSymbol(bytes)->moduleIndex = 1;
  Check(!ttr::ParseManifest(bytes, view, error), "invalid module index");
  bytes = deterministicA;
  FirstSymbol(bytes)->symbolId = 0xffff;
  Check(!ttr::ParseManifest(bytes, view, error), "invalid symbol index");
  bytes = deterministicA;
  FirstSymbol(bytes)->kind = 0xff;
  Check(!ttr::ParseManifest(bytes, view, error), "invalid symbol kind");
  bytes = deterministicA;
  Record(bytes)->symbolOffset = Record(bytes)->moduleOffset;
  Check(!ttr::ParseManifest(bytes, view, error), "overlapping record data");
  bytes = deterministicA;
  Record(bytes)->moduleCount = UINT32_MAX;
  Check(!ttr::ParseManifest(bytes, view, error), "module table overflow or excess");
}

void SelectionCases()
{
  auto bytes = OneRecord();
  ttr::ManifestView view;
  std::string error;
  Check(ttr::ParseManifest(bytes, view, error), "selection manifest parses");
  const auto matching = Module();
  bool ambiguous = true;
  const auto* selected = ttr::SelectExactRecord(view, std::span{&matching, 1}, ambiguous);
  Check(selected && selected->recordId == 42 && !ambiguous, "exact identity selects record");
  auto other = matching;
  other.pdbAge = 2;
  selected = ttr::SelectExactRecord(view, std::span{&other, 1}, ambiguous);
  Check(!selected && !ambiguous, "nonmatching identity selects nothing");
}

} // namespace

int main()
{
  ParseCases();
  SelectionCases();
  return failures ? 1 : 0;
}
