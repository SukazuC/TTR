#include "ttr_manifest.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
int failures{};

void Check(bool value, const char* name)
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

ttr::ManifestSymbolV2 Symbol(ttr::SymbolId id)
{
  ttr::ManifestSymbolV2 symbol{};
  symbol.symbolId = static_cast<std::uint16_t>(id);
  symbol.moduleIndex = 0;
  symbol.kind = static_cast<std::uint8_t>(ttr::SymbolKind::Function);
  symbol.required = 1;
  symbol.rva = 0x1000 + static_cast<std::uint32_t>(symbol.symbolId) * 16;
  return symbol;
}

std::vector<ttr::ManifestSymbolV2> SharedSymbols()
{
  using S = ttr::SymbolId;
  return {Symbol(S::TaskGroup_GetNumItems),
          Symbol(S::TaskListWnd_GetTBGroupFromGroup),
          Symbol(S::TaskBtnGroup_GetGroupType),
          Symbol(S::TaskBtnGroup_IndexOfTaskItem),
          Symbol(S::TaskListWnd_TaskInclusionChanged),
          Symbol(S::TaskItemFilter_IsTaskAllowed)};
}

std::vector<ttr::ManifestSymbolV2> ClassicSymbols()
{
  using S = ttr::SymbolId;
  auto symbols = SharedSymbols();
  for (const auto id : {S::TaskListThumbnailWnd_GetHoverIndex, S::TaskListThumbnailWnd_GetTaskItem,
                        S::TaskListThumbnailWnd_GetTaskGroup, S::TaskListThumbnailWnd_TaskReordered,
                        S::TaskListThumbnailWnd_WndProc})
    symbols.push_back(Symbol(id));
  return symbols;
}

std::vector<ttr::ManifestSymbolV2> XamlSymbols()
{
  using S = ttr::SymbolId;
  auto symbols = SharedSymbols();
  for (const auto id :
       {S::TaskItemThumbnail_ConstructorV2, S::TaskGroup_Thumbnails,
        S::TaskItemThumbnailVector_Size, S::TaskItemThumbnailVector_GetAt,
        S::TaskListWnd_HandleExtendedUIClick, S::HoverFlyoutModel_TargetItemKey,
        S::TaskItemThumbnailList_OnPointerMoved, S::FlyoutFrame_UpdateFlyoutPosition})
    symbols.push_back(Symbol(id));
  return symbols;
}

ttr::ManifestAdjustmentV2 Adjustment()
{
  ttr::ManifestAdjustmentV2 adjustment{};
  adjustment.adjustmentId = static_cast<std::uint16_t>(ttr::AdjustmentId::TaskListWnd_ITaskListUI);
  adjustment.moduleIndex = 0;
  adjustment.required = 1;
  adjustment.offset = 8;
  adjustment.objectSize = 64;
  return adjustment;
}

std::vector<std::byte> EmptyManifest()
{
  std::vector<std::byte> bytes(sizeof(ttr::ManifestHeaderV2));
  auto* header = reinterpret_cast<ttr::ManifestHeaderV2*>(bytes.data());
  std::memcpy(header->magic, ttr::kManifestMagic, sizeof(header->magic));
  header->formatVersion = ttr::kManifestVersion;
  header->headerSize = sizeof(*header);
  header->totalSize = static_cast<std::uint32_t>(bytes.size());
  header->sequence = 1;
  header->recordTableOffset = sizeof(*header);
  return bytes;
}

std::vector<std::byte> RecordManifest(std::uint32_t backends,
                                      const std::vector<ttr::ManifestSymbolV2>& symbols,
                                      const std::vector<ttr::ManifestAdjustmentV2>& adjustments)
{
  std::vector<std::byte> bytes(sizeof(ttr::ManifestHeaderV2) + sizeof(ttr::ManifestRecordV2));
  ttr::ManifestRecordV2 record{};
  record.recordId = 42;
  record.backendFlags = backends;
  record.minimumProtocolVersion = 1;
  record.moduleCount = 1;
  record.moduleOffset = static_cast<std::uint32_t>(bytes.size());
  Append(bytes, Module());
  record.symbolCount = static_cast<std::uint32_t>(symbols.size());
  record.symbolOffset = static_cast<std::uint32_t>(bytes.size());
  for (const auto& symbol : symbols)
    Append(bytes, symbol);
  record.adjustmentCount = static_cast<std::uint32_t>(adjustments.size());
  if (!adjustments.empty())
  {
    record.adjustmentOffset = static_cast<std::uint32_t>(bytes.size());
    for (const auto& adjustment : adjustments)
      Append(bytes, adjustment);
  }
  auto* header = reinterpret_cast<ttr::ManifestHeaderV2*>(bytes.data());
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

ttr::ManifestRecordV2* Record(std::vector<std::byte>& bytes)
{
  auto* header = reinterpret_cast<ttr::ManifestHeaderV2*>(bytes.data());
  return reinterpret_cast<ttr::ManifestRecordV2*>(bytes.data() + header->recordTableOffset);
}

ttr::ManifestSymbolV2* Symbols(std::vector<std::byte>& bytes)
{
  return reinterpret_cast<ttr::ManifestSymbolV2*>(bytes.data() + Record(bytes)->symbolOffset);
}

ttr::ManifestAdjustmentV2* Adjustments(std::vector<std::byte>& bytes)
{
  return reinterpret_cast<ttr::ManifestAdjustmentV2*>(bytes.data() +
                                                      Record(bytes)->adjustmentOffset);
}

bool Parses(const std::vector<std::byte>& bytes)
{
  ttr::ManifestView view;
  std::string error;
  return ttr::ParseManifest(bytes, view, error);
}

void ParserAndMigrationCases()
{
  auto bytes = EmptyManifest();
  Check(Parses(bytes), "valid empty v2 manifest");
  bytes[0] = std::byte{'X'};
  Check(!Parses(bytes), "malformed magic");
  bytes = EmptyManifest();
  reinterpret_cast<ttr::ManifestHeaderV2*>(bytes.data())->totalSize++;
  Check(!Parses(bytes), "malformed total size");
  bytes = EmptyManifest();
  auto* header = reinterpret_cast<ttr::ManifestHeaderV2*>(bytes.data());
  header->formatVersion = 1;
  header->magic[6] = '1';
  Check(!Parses(bytes), "v1 manifest requires explicit migration");
  bytes = EmptyManifest();
  header = reinterpret_cast<ttr::ManifestHeaderV2*>(bytes.data());
  header->recordCount = UINT32_MAX;
  Check(!Parses(bytes), "excessive record count");
}

void BackendCases()
{
  auto classic = RecordManifest(ttr::BackendClassic, ClassicSymbols(), {Adjustment()});
  Check(Parses(classic), "valid classic record with typed adjustment");
  auto xaml = RecordManifest(ttr::BackendXaml, XamlSymbols(), {});
  Check(Parses(xaml), "valid constructor-context XAML record without adjustment");
  auto hybridSymbols = ClassicSymbols();
  auto xamlSymbols = XamlSymbols();
  for (const auto& symbol : xamlSymbols)
    if (std::none_of(hybridSymbols.begin(), hybridSymbols.end(),
                     [&](const auto& existing) { return existing.symbolId == symbol.symbolId; }))
      hybridSymbols.push_back(symbol);
  Check(
      Parses(RecordManifest(ttr::BackendClassic | ttr::BackendXaml, hybridSymbols, {Adjustment()})),
      "valid hybrid record");
  Check(!Parses(RecordManifest(ttr::BackendClassic, ClassicSymbols(), {})),
        "classic rejects missing adjustment");
  Check(!Parses(RecordManifest(ttr::BackendXaml, XamlSymbols(), {Adjustment()})),
        "XAML rejects unused classic adjustment");
  auto missing = XamlSymbols();
  missing.erase(missing.begin() + 6);
  Check(!Parses(RecordManifest(ttr::BackendXaml, missing, {})),
        "XAML rejects incomplete symbol group");
  auto unused = XamlSymbols();
  unused.push_back(Symbol(ttr::SymbolId::TaskListThumbnailWnd_WndProc));
  Check(!Parses(RecordManifest(ttr::BackendXaml, unused, {})),
        "XAML rejects unused classic symbol");
}

void AdjustmentCases()
{
  auto bytes = RecordManifest(ttr::BackendClassic, ClassicSymbols(), {Adjustment()});
  Adjustments(bytes)->offset = 3;
  Check(!Parses(bytes), "unaligned adjustment rejected");
  bytes = RecordManifest(ttr::BackendClassic, ClassicSymbols(), {Adjustment()});
  Adjustments(bytes)->offset = 64;
  Check(!Parses(bytes), "out-of-bounds adjustment rejected");
  bytes = RecordManifest(ttr::BackendClassic, ClassicSymbols(), {Adjustment()});
  Adjustments(bytes)->moduleIndex = 1;
  Check(!Parses(bytes), "adjustment module mismatch rejected");
  bytes = RecordManifest(ttr::BackendClassic, ClassicSymbols(), {Adjustment()});
  Adjustments(bytes)->required = 0;
  Check(!Parses(bytes), "optional required adjustment rejected");
  bytes = RecordManifest(ttr::BackendXaml, XamlSymbols(), {});
  Symbols(bytes)->required = 0;
  Check(!Parses(bytes), "optional required symbol rejected");
}

void SelectionCases()
{
  auto bytes = RecordManifest(ttr::BackendXaml, XamlSymbols(), {});
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
  Check(!selected && !ambiguous, "identity mismatch rejects record");
}
} // namespace

int main()
{
  ParserAndMigrationCases();
  BackendCases();
  AdjustmentCases();
  SelectionCases();
  return failures ? 1 : 0;
}
