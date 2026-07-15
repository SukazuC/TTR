#include "ttr_manifest.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace ttr {
bool CheckedRange(std::size_t offset, std::size_t size, std::size_t total) noexcept {
  return offset <= total && size <= total - offset;
}

template<class T>
static std::span<const T> SpanAt(std::span<const std::byte> bytes, std::uint32_t offset,
                                 std::uint32_t count) noexcept {
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) return {};
  const auto size = static_cast<std::size_t>(count) * sizeof(T);
  if (!CheckedRange(offset, size, bytes.size())) return {};
  return {reinterpret_cast<const T*>(bytes.data() + offset), count};
}

bool ParseManifest(std::span<const std::byte> bytes, ManifestView& out, std::string& error) noexcept {
  out = {};
  if (bytes.size() < sizeof(ManifestHeaderV1) || bytes.size() > kMaxManifestBytes) {
    error = "manifest size is invalid"; return false;
  }
  const auto* h = reinterpret_cast<const ManifestHeaderV1*>(bytes.data());
  if (std::memcmp(h->magic, kManifestMagic, sizeof(kManifestMagic)) != 0) {
    error = "manifest magic is invalid"; return false;
  }
  if (h->formatVersion != kManifestVersion || h->headerSize != sizeof(*h)) {
    error = "manifest version is unsupported"; return false;
  }
  if (h->totalSize != bytes.size() || h->recordCount > 4096) {
    error = "manifest header bounds are invalid"; return false;
  }
  auto records = SpanAt<ManifestRecordV1>(bytes, h->recordTableOffset, h->recordCount);
  if (records.size() != h->recordCount) { error = "record table is out of bounds"; return false; }
  ManifestView candidate{h, bytes, records};
  std::unordered_set<std::uint64_t> ids;
  for (const auto& record : records) {
    if (!ids.insert(record.recordId).second) { error = "duplicate record id"; return false; }
    if (!ValidateRecord(candidate, record, error)) return false;
  }
  out = candidate;
  return true;
}

std::span<const ModuleIdentityV1> RecordModules(const ManifestView& view,
                                                const ManifestRecordV1& record) noexcept {
  return SpanAt<ModuleIdentityV1>(view.bytes, record.moduleOffset, record.moduleCount);
}

std::span<const ManifestSymbolV1> RecordSymbols(const ManifestView& view,
                                                const ManifestRecordV1& record) noexcept {
  return SpanAt<ManifestSymbolV1>(view.bytes, record.symbolOffset, record.symbolCount);
}

bool ValidateRecord(const ManifestView& view, const ManifestRecordV1& record,
                    std::string& error) noexcept {
  if (!record.recordId || record.minimumProtocolVersion != 1 ||
      record.moduleCount == 0 || record.moduleCount > 16 || record.symbolCount > 128 ||
      !(record.backendFlags & (BackendClassic | BackendXaml))) {
    error = "record metadata is invalid"; return false;
  }
  const auto modules = RecordModules(view, record);
  const auto symbols = RecordSymbols(view, record);
  if (modules.size() != record.moduleCount || symbols.size() != record.symbolCount) {
    error = "record data is out of bounds"; return false;
  }
  std::unordered_set<std::uint16_t> symbolIds;
  for (const auto& module : modules) {
    if (!std::memchr(module.baseName, '\0', sizeof(module.baseName)) ||
        !module.baseName[0] || !module.sizeOfImage) {
      error = "module identity is invalid"; return false;
    }
  }
  for (const auto& symbol : symbols) {
    if (symbol.symbolId == 0 || symbol.symbolId > static_cast<std::uint16_t>(SymbolId::Last) ||
        symbol.moduleIndex >= modules.size() || symbol.rva >= modules[symbol.moduleIndex].sizeOfImage ||
        (symbol.kind != static_cast<std::uint8_t>(SymbolKind::Function) &&
         symbol.kind != static_cast<std::uint8_t>(SymbolKind::ReadOnlyData)) ||
        !symbolIds.insert(symbol.symbolId).second) {
      error = "symbol record is invalid"; return false;
    }
  }
  auto has = [&](SymbolId id) { return symbolIds.contains(static_cast<std::uint16_t>(id)); };
  const bool common = has(SymbolId::TaskListWnd_Vtable_ITaskListUI) &&
      has(SymbolId::TaskGroup_GetNumItems) && has(SymbolId::TaskListWnd_GetTBGroupFromGroup) &&
      has(SymbolId::TaskBtnGroup_GetGroupType) && has(SymbolId::TaskBtnGroup_IndexOfTaskItem) &&
      has(SymbolId::TaskListWnd_TaskInclusionChanged) && has(SymbolId::TaskItemFilter_IsTaskAllowed);
  const bool classic = common && has(SymbolId::TaskListThumbnailWnd_GetHoverIndex) &&
      has(SymbolId::TaskListThumbnailWnd_GetTaskItem) && has(SymbolId::TaskListThumbnailWnd_GetTaskGroup) &&
      has(SymbolId::TaskListThumbnailWnd_TaskReordered) && has(SymbolId::TaskListThumbnailWnd_WndProc);
  const bool xaml = common && (has(SymbolId::TaskItemThumbnail_ConstructorV1) ||
      has(SymbolId::TaskItemThumbnail_ConstructorV2)) && has(SymbolId::TaskGroup_Thumbnails) &&
      has(SymbolId::TaskItemThumbnailVector_Size) && has(SymbolId::TaskItemThumbnailVector_GetAt) &&
      has(SymbolId::TaskListWnd_HandleExtendedUIClick) && has(SymbolId::HoverFlyoutModel_TargetItemKey) &&
      has(SymbolId::TaskItemThumbnailList_OnPointerMoved) && has(SymbolId::FlyoutFrame_UpdateFlyoutPosition);
  if (((record.backendFlags & BackendClassic) && !classic) ||
      ((record.backendFlags & BackendXaml) && !xaml) ||
      ((record.backendFlags & BackendAnimatedXaml) && !(record.backendFlags & BackendXaml))) {
    error = "required backend symbol group is incomplete"; return false;
  }
  return true;
}
}
