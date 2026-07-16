#include "ttr_manifest.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_set>

namespace ttr
{
namespace
{

struct Range
{
  std::size_t begin{};
  std::size_t end{};
};

template <class T>
std::span<const T> SpanAt(const std::span<const std::byte> bytes, const std::uint32_t offset,
                          const std::uint32_t count) noexcept
{
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
  {
    return {};
  }
  const auto size = static_cast<std::size_t>(count) * sizeof(T);
  if (!CheckedRange(offset, size, bytes.size()))
  {
    return {};
  }
  return {reinterpret_cast<const T*>(bytes.data() + offset), count};
}

template <typename T>
bool AddRange(const std::uint32_t offset, const std::uint32_t count, const std::size_t fixedEnd,
              const std::size_t total, std::vector<Range>& ranges, std::string& error)
{
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
  {
    error = "manifest table size overflows";
    return false;
  }
  const auto bytes = static_cast<std::size_t>(count) * sizeof(T);
  if (!bytes || offset < fixedEnd || !CheckedRange(offset, bytes, total))
  {
    error = "record data range is invalid or overlaps fixed tables";
    return false;
  }
  const Range candidate{offset, static_cast<std::size_t>(offset) + bytes};
  if (std::any_of(ranges.begin(), ranges.end(), [&](const Range& existing) {
        return candidate.begin < existing.end && existing.begin < candidate.end;
      }))
  {
    error = "record data ranges overlap or are duplicated";
    return false;
  }
  ranges.push_back(candidate);
  return true;
}

bool GuidIsZero(const GuidBytes& guid) noexcept
{
  return std::all_of(std::begin(guid.value), std::end(guid.value),
                     [](const std::uint8_t value) { return value == 0; });
}

std::string IdentityKey(const ModuleIdentityV1& module)
{
  std::string key(module.baseName);
  key.append(reinterpret_cast<const char*>(&module.timeDateStamp), sizeof(module.timeDateStamp));
  key.append(reinterpret_cast<const char*>(&module.sizeOfImage), sizeof(module.sizeOfImage));
  key.append(reinterpret_cast<const char*>(module.pdbGuid.value), sizeof(module.pdbGuid.value));
  key.append(reinterpret_cast<const char*>(&module.pdbAge), sizeof(module.pdbAge));
  return key;
}

std::vector<std::string> ModuleSetKey(const ManifestView& view, const ManifestRecordV2& record)
{
  std::vector<std::string> result;
  for (const auto& module : RecordModules(view, record))
  {
    result.push_back(IdentityKey(module));
  }
  std::sort(result.begin(), result.end());
  return result;
}

} // namespace

bool CheckedRange(const std::size_t offset, const std::size_t size,
                  const std::size_t total) noexcept
{
  return offset <= total && size <= total - offset;
}

bool ParseManifest(const std::span<const std::byte> bytes, ManifestView& out,
                   std::string& error) noexcept
{
  out = {};
  error.clear();
  if (bytes.size() < sizeof(ManifestHeaderV2) || bytes.size() > kMaxManifestBytes)
  {
    error = "manifest size is invalid";
    return false;
  }
  const auto* header = reinterpret_cast<const ManifestHeaderV2*>(bytes.data());
  if (std::memcmp(header->magic, kManifestMagic, sizeof(kManifestMagic)) != 0)
  {
    error = "manifest magic is invalid";
    return false;
  }
  if (header->formatVersion != kManifestVersion || header->headerSize != sizeof(*header))
  {
    error = "manifest version or header size is unsupported";
    return false;
  }
  if (header->totalSize != bytes.size() || header->recordCount > 4096 ||
      header->recordTableOffset < header->headerSize)
  {
    error = "manifest header bounds are invalid";
    return false;
  }
  if (header->recordCount > std::numeric_limits<std::size_t>::max() / sizeof(ManifestRecordV2))
  {
    error = "record table size overflows";
    return false;
  }
  const auto recordBytes = static_cast<std::size_t>(header->recordCount) * sizeof(ManifestRecordV2);
  if (!CheckedRange(header->recordTableOffset, recordBytes, bytes.size()))
  {
    error = "record table is out of bounds";
    return false;
  }
  const auto records =
      SpanAt<ManifestRecordV2>(bytes, header->recordTableOffset, header->recordCount);
  if (records.size() != header->recordCount)
  {
    error = "record table is out of bounds";
    return false;
  }
  ManifestView candidate{header, bytes, records};
  const auto fixedEnd = static_cast<std::size_t>(header->recordTableOffset) + recordBytes;
  std::vector<Range> ranges;
  std::unordered_set<std::uint64_t> ids;
  std::set<std::vector<std::string>> moduleSets;
  for (const auto& record : records)
  {
    if (!ids.insert(record.recordId).second)
    {
      error = "duplicate record id";
      return false;
    }
    if (!AddRange<ModuleIdentityV1>(record.moduleOffset, record.moduleCount, fixedEnd, bytes.size(),
                                    ranges, error) ||
        (record.symbolCount &&
         !AddRange<ManifestSymbolV2>(record.symbolOffset, record.symbolCount, fixedEnd,
                                     bytes.size(), ranges, error)) ||
        (record.adjustmentCount &&
         !AddRange<ManifestAdjustmentV2>(record.adjustmentOffset, record.adjustmentCount, fixedEnd,
                                         bytes.size(), ranges, error)))
    {
      return false;
    }
    if (!ValidateRecord(candidate, record, error))
    {
      return false;
    }
    if (!moduleSets.insert(ModuleSetKey(candidate, record)).second)
    {
      error = "multiple records match the same exact module identity set";
      return false;
    }
  }
  out = candidate;
  return true;
}

std::span<const ModuleIdentityV1> RecordModules(const ManifestView& view,
                                                const ManifestRecordV2& record) noexcept
{
  return SpanAt<ModuleIdentityV1>(view.bytes, record.moduleOffset, record.moduleCount);
}

std::span<const ManifestSymbolV2> RecordSymbols(const ManifestView& view,
                                                const ManifestRecordV2& record) noexcept
{
  return SpanAt<ManifestSymbolV2>(view.bytes, record.symbolOffset, record.symbolCount);
}

std::span<const ManifestAdjustmentV2> RecordAdjustments(const ManifestView& view,
                                                        const ManifestRecordV2& record) noexcept
{
  return SpanAt<ManifestAdjustmentV2>(view.bytes, record.adjustmentOffset, record.adjustmentCount);
}

bool ValidateRecord(const ManifestView& view, const ManifestRecordV2& record,
                    std::string& error) noexcept
{
  constexpr auto knownBackends = BackendClassic | BackendXaml | BackendAnimatedXaml;
  if (!record.recordId || record.minimumProtocolVersion != 1 || record.moduleCount == 0 ||
      record.moduleCount > 16 || record.symbolCount > 128 || record.adjustmentCount > 8 ||
      !(record.backendFlags & (BackendClassic | BackendXaml)) ||
      (record.backendFlags & ~knownBackends) != 0)
  {
    error = "record metadata is invalid";
    return false;
  }
  const auto modules = RecordModules(view, record);
  const auto symbols = RecordSymbols(view, record);
  const auto adjustments = RecordAdjustments(view, record);
  if (modules.size() != record.moduleCount || symbols.size() != record.symbolCount ||
      adjustments.size() != record.adjustmentCount)
  {
    error = "record data is out of bounds";
    return false;
  }
  std::unordered_set<std::string> moduleIdentities;
  for (const auto& module : modules)
  {
    const auto* terminator =
        static_cast<const char*>(std::memchr(module.baseName, '\0', sizeof(module.baseName)));
    if (!terminator || terminator == module.baseName || !module.sizeOfImage || !module.pdbAge ||
        GuidIsZero(module.pdbGuid))
    {
      error = "module identity is invalid or incomplete";
      return false;
    }
    for (const char* character = module.baseName; character != terminator; ++character)
    {
      if (static_cast<unsigned char>(*character) > 0x7f)
      {
        error = "module base name is not ASCII";
        return false;
      }
    }
    if (!moduleIdentities.insert(IdentityKey(module)).second)
    {
      error = "record contains a duplicate module identity";
      return false;
    }
  }
  std::unordered_set<std::uint16_t> symbolIds;
  for (const auto& symbol : symbols)
  {
    if (symbol.symbolId == 0 || symbol.symbolId > static_cast<std::uint16_t>(SymbolId::Last) ||
        symbol.moduleIndex >= modules.size() ||
        symbol.rva >= modules[symbol.moduleIndex].sizeOfImage || !symbol.rva ||
        (symbol.kind != static_cast<std::uint8_t>(SymbolKind::Function) &&
         symbol.kind != static_cast<std::uint8_t>(SymbolKind::ReadOnlyData)) ||
        symbol.required != 1 ||
        std::any_of(std::begin(symbol.reserved), std::end(symbol.reserved),
                    [](const std::uint8_t value) { return value != 0; }) ||
        !symbolIds.insert(symbol.symbolId).second)
    {
      error = "symbol record is invalid";
      return false;
    }
  }
  const auto has = [&](const SymbolId id) {
    return symbolIds.contains(static_cast<std::uint16_t>(id));
  };
  std::unordered_set<std::uint16_t> adjustmentIds;
  for (const auto& adjustment : adjustments)
  {
    if (adjustment.adjustmentId == 0 ||
        adjustment.adjustmentId > static_cast<std::uint16_t>(AdjustmentId::Last) ||
        adjustment.moduleIndex >= modules.size() || adjustment.required != 1 ||
        adjustment.objectSize < sizeof(void*) || adjustment.objectSize > 1024 * 1024 ||
        adjustment.offset % alignof(void*) != 0 ||
        adjustment.offset > adjustment.objectSize - sizeof(void*) ||
        !adjustmentIds.insert(adjustment.adjustmentId).second)
    {
      error = "adjustment record is invalid";
      return false;
    }
  }
  const auto hasAdjustment = [&](const AdjustmentId id) {
    return adjustmentIds.contains(static_cast<std::uint16_t>(id));
  };
  const bool common =
      has(SymbolId::TaskGroup_GetNumItems) && has(SymbolId::TaskListWnd_GetTBGroupFromGroup) &&
      has(SymbolId::TaskBtnGroup_GetGroupType) && has(SymbolId::TaskBtnGroup_IndexOfTaskItem) &&
      has(SymbolId::TaskListWnd_TaskInclusionChanged) &&
      has(SymbolId::TaskItemFilter_IsTaskAllowed);
  const bool classic = common && has(SymbolId::TaskListThumbnailWnd_GetHoverIndex) &&
                       has(SymbolId::TaskListThumbnailWnd_GetTaskItem) &&
                       has(SymbolId::TaskListThumbnailWnd_GetTaskGroup) &&
                       has(SymbolId::TaskListThumbnailWnd_TaskReordered) &&
                       has(SymbolId::TaskListThumbnailWnd_WndProc) &&
                       hasAdjustment(AdjustmentId::TaskListWnd_ITaskListUI);
  const bool xaml = common &&
                    (has(SymbolId::TaskItemThumbnail_ConstructorV1) ||
                     has(SymbolId::TaskItemThumbnail_ConstructorV2)) &&
                    has(SymbolId::TaskGroup_Thumbnails) &&
                    has(SymbolId::TaskItemThumbnailVector_Size) &&
                    has(SymbolId::TaskItemThumbnailVector_GetAt) &&
                    has(SymbolId::TaskListWnd_HandleExtendedUIClick) &&
                    has(SymbolId::HoverFlyoutModel_TargetItemKey) &&
                    has(SymbolId::TaskItemThumbnailList_OnPointerMoved) &&
                    has(SymbolId::FlyoutFrame_UpdateFlyoutPosition);
  if (((record.backendFlags & BackendClassic) && !classic) ||
      ((record.backendFlags & BackendXaml) && !xaml) ||
      ((record.backendFlags & BackendAnimatedXaml) && !(record.backendFlags & BackendXaml)))
  {
    error = "required backend symbol group is incomplete";
    return false;
  }
  const auto sharedSymbol = [&](const SymbolId id) {
    return id == SymbolId::TaskGroup_GetNumItems ||
           id == SymbolId::TaskListWnd_GetTBGroupFromGroup ||
           id == SymbolId::TaskBtnGroup_GetGroupType ||
           id == SymbolId::TaskBtnGroup_IndexOfTaskItem ||
           id == SymbolId::TaskListWnd_TaskInclusionChanged ||
           id == SymbolId::TaskItemFilter_IsTaskAllowed;
  };
  const auto classicSymbol = [&](const SymbolId id) {
    return id == SymbolId::TaskListThumbnailWnd_GetHoverIndex ||
           id == SymbolId::TaskListThumbnailWnd_GetTaskItem ||
           id == SymbolId::TaskListThumbnailWnd_GetTaskGroup ||
           id == SymbolId::TaskListThumbnailWnd_TaskReordered ||
           id == SymbolId::TaskListThumbnailWnd_WndProc;
  };
  const auto xamlSymbol = [&](const SymbolId id) {
    return id == SymbolId::TaskItemThumbnail_ConstructorV1 ||
           id == SymbolId::TaskItemThumbnail_ConstructorV2 ||
           id == SymbolId::TaskGroup_Thumbnails || id == SymbolId::TaskItemThumbnailVector_Size ||
           id == SymbolId::TaskItemThumbnailVector_GetAt ||
           id == SymbolId::TaskListWnd_HandleExtendedUIClick ||
           id == SymbolId::HoverFlyoutModel_TargetItemKey ||
           id == SymbolId::TaskItemThumbnailList_OnPointerMoved ||
           id == SymbolId::FlyoutFrame_UpdateFlyoutPosition;
  };
  for (const auto& symbol : symbols)
  {
    const auto id = static_cast<SymbolId>(symbol.symbolId);
    if (!sharedSymbol(id) && (!classicSymbol(id) || !(record.backendFlags & BackendClassic)) &&
        (!xamlSymbol(id) || !(record.backendFlags & BackendXaml)))
    {
      error = "record contains a symbol unused by its selected backends";
      return false;
    }
  }
  if (!adjustments.empty() && !(record.backendFlags & BackendClassic))
  {
    error = "record contains an adjustment unused by its selected backends";
    return false;
  }
  return true;
}

bool ModuleIdentityEqual(const ModuleIdentityV1& left, const ModuleIdentityV1& right) noexcept
{
  return _stricmp(left.baseName, right.baseName) == 0 &&
         left.timeDateStamp == right.timeDateStamp && left.sizeOfImage == right.sizeOfImage &&
         left.pdbAge == right.pdbAge &&
         std::memcmp(left.pdbGuid.value, right.pdbGuid.value, sizeof(left.pdbGuid.value)) == 0;
}

const ManifestRecordV2* SelectExactRecord(const ManifestView& view,
                                          const std::span<const ModuleIdentityV1> loaded,
                                          bool& ambiguous) noexcept
{
  ambiguous = false;
  const ManifestRecordV2* result = nullptr;
  for (const auto& record : view.records)
  {
    const auto expected = RecordModules(view, record);
    const bool matches =
        std::all_of(expected.begin(), expected.end(), [&](const ModuleIdentityV1& identity) {
          return std::any_of(loaded.begin(), loaded.end(), [&](const ModuleIdentityV1& actual) {
            return ModuleIdentityEqual(identity, actual);
          });
        });
    if (!matches)
    {
      continue;
    }
    if (result)
    {
      ambiguous = true;
      return nullptr;
    }
    result = &record;
  }
  return result;
}

} // namespace ttr
