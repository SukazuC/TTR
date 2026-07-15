#include "task_model.h"
#include "payload_log.h"
#include "reorder_transaction.h"
#include "seh_filter.h"
#include "ttr_array_move.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace ttr::payload
{
namespace
{
struct Dpa
{
  int count;
  void** items;
  HANDLE heap;
  int capacity;
  int grow;
};
struct TaskbarTarget
{
  Dpa* buttons{};
  int fromIndex{-1};
  int toIndex{-1};
  void* adjustedUi{};
};
using GetNumItemsFn = int(WINAPI*)(void*);
using GetGroupFn = void*(WINAPI*)(void*, void*, int*);
using GetTypeFn = int(WINAPI*)(void*);
using IndexFn = int(WINAPI*)(void*, void*);
using ChangedFn = HRESULT(WINAPI*)(void*, void*, void*);
GetNumItemsFn getNum{};
GetGroupFn getGroup{};
GetTypeFn getType{};
IndexFn indexOf{};
ChangedFn changed{};
bool hasUiAdjustment{};
std::uint32_t uiAdjustment{};
std::uint32_t taskListObjectSize{};
std::atomic_bool filterGate{};
std::atomic<DWORD> captureThread{};
void* capturedDpa{};
ULONGLONG lastRefresh{};

bool ValidDpa(const Dpa* dpa)
{
  return dpa && dpa->count >= 0 && dpa->count <= 1024 && dpa->capacity >= dpa->count &&
         dpa->capacity <= 4096 && (!dpa->count || dpa->items);
}

int CaptureIndex(void* buttonGroup, void* item, Dpa*& array)
{
  capturedDpa = nullptr;
  captureThread.store(GetCurrentThreadId(), std::memory_order_release);
  int result = -1;
  __try
  {
    result = indexOf(buttonGroup, item);
  }
  __finally
  {
    captureThread.store(0, std::memory_order_release);
  }
  array = static_cast<Dpa*>(capturedDpa);
  return result;
}

Dpa* TaskItems(void* group)
{
  if (!group || !getNum)
    return nullptr;
  static size_t offset = SIZE_MAX;
  if (offset == SIZE_MAX)
  {
    int values[128]{};
    int* ptrs[128]{};
    for (int i = 0; i < 128; ++i)
    {
      values[i] = i;
      ptrs[i] = &values[i];
    }
    int discovered = -1;
    __try
    {
      discovered = getNum(ptrs);
    }
    __except (SehFilter(GetExceptionCode()))
    {
      return nullptr;
    }
    if (discovered < 0 || discovered >= 128)
      return nullptr;
    offset = static_cast<size_t>(discovered);
  }
  auto* dpa = static_cast<Dpa*>(static_cast<void**>(group)[offset]);
  if (!ValidDpa(dpa))
    return nullptr;
  int publicCount = -1;
  __try
  {
    publicCount = getNum(group);
  }
  __except (SehFilter(GetExceptionCode()))
  {
    return nullptr;
  }
  return publicCount == dpa->count ? dpa : nullptr;
}

bool CollectTaskbar(HWND tray, bool secondary, void* group, void* from, void* to,
                    std::vector<TaskbarTarget>& targets)
{
  HWND switcher = secondary ? FindWindowExW(tray, nullptr, L"WorkerW", nullptr)
                            : reinterpret_cast<HWND>(GetPropW(tray, L"TaskbandHWND"));
  if (!switcher)
    return false;
  HWND list = FindWindowExW(switcher, nullptr, L"MSTaskListWClass", nullptr);
  if (!list)
    return false;
  void* object = reinterpret_cast<void*>(GetWindowLongPtrW(list, 0));
  if (!object)
    return false;
  void* buttonGroup = getGroup(object, group, nullptr);
  if (!buttonGroup)
    return true;
  const int type = getType(buttonGroup);
  if (type != 1 && type != 3)
    return false;
  Dpa* buttons{};
  const int fromIndex = CaptureIndex(buttonGroup, from, buttons);
  Dpa* second{};
  const int toIndex = CaptureIndex(buttonGroup, to, second);
  if (!ValidDpa(buttons) || second != buttons || fromIndex < 0 || toIndex < 0 ||
      fromIndex >= buttons->count || toIndex >= buttons->count || fromIndex == toIndex)
    return false;
  if (std::any_of(targets.begin(), targets.end(),
                  [&](const TaskbarTarget& target) { return target.buttons == buttons; }))
    return false;
  void* adjustedUi = nullptr;
  if (hasUiAdjustment)
  {
    if (uiAdjustment > taskListObjectSize - sizeof(void*))
      return false;
    adjustedUi = static_cast<std::byte*>(object) + uiAdjustment;
  }
  targets.push_back({buttons, fromIndex, toIndex, adjustedUi});
  return true;
}

bool Notify(void* ui, void* group, void* item)
{
  if (!ui)
    return false;
  bool ok = false;
  HWND captured = GetCapture();
  if (captured)
    SendMessageW(captured, WM_SETREDRAW, FALSE, 0);
  __try
  {
    filterGate.store(true, std::memory_order_release);
    const HRESULT first = changed(ui, group, item);
    filterGate.store(false, std::memory_order_release);
    const HRESULT second = changed(ui, group, item);
    ok = SUCCEEDED(first) && SUCCEEDED(second);
    if (ok)
      lastRefresh = GetTickCount64();
  }
  __except (SehFilter(GetExceptionCode()))
  {
    ok = false;
  }
  filterGate.store(false, std::memory_order_release);
  if (captured)
  {
    SendMessageW(captured, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(captured, nullptr, nullptr,
                 RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
  }
  return ok;
}

} // namespace

bool ConfigureTaskModel(const Compatibility& compatibility) noexcept
{
  auto get = [&](SymbolId id) { return compatibility.symbols[static_cast<size_t>(id)]; };
  getNum = reinterpret_cast<GetNumItemsFn>(get(SymbolId::TaskGroup_GetNumItems));
  getGroup = reinterpret_cast<GetGroupFn>(get(SymbolId::TaskListWnd_GetTBGroupFromGroup));
  getType = reinterpret_cast<GetTypeFn>(get(SymbolId::TaskBtnGroup_GetGroupType));
  indexOf = reinterpret_cast<IndexFn>(get(SymbolId::TaskBtnGroup_IndexOfTaskItem));
  changed = reinterpret_cast<ChangedFn>(get(SymbolId::TaskListWnd_TaskInclusionChanged));
  hasUiAdjustment = compatibility.hasTaskListUiAdjustment;
  uiAdjustment = compatibility.taskListUiOffset;
  taskListObjectSize = compatibility.taskListObjectSize;
  return getNum && getGroup && getType && indexOf && changed &&
         (!hasUiAdjustment ||
          (taskListObjectSize >= sizeof(void*) && uiAdjustment % alignof(void*) == 0 &&
           uiAdjustment <= taskListObjectSize - sizeof(void*)));
}

bool MoveTaskInGroupImpl(void* group, void* from, void* to, void* constructorUi)
{
  auto* groupItems = TaskItems(group);
  if (!ValidDpa(groupItems))
    return false;
  int groupFrom = -1;
  int groupTo = -1;
  for (int i = 0; i < groupItems->count; ++i)
  {
    if (groupItems->items[i] == from)
      groupFrom = i;
    if (groupItems->items[i] == to)
      groupTo = i;
  }
  if (groupFrom < 0 || groupTo < 0 || groupFrom == groupTo)
    return false;

  struct Context
  {
    void* group;
    void* from;
    void* to;
    std::vector<TaskbarTarget> targets;
    bool valid{true};
  } context{group, from, to};
  EnumThreadWindows(
      GetCurrentThreadId(),
      [](HWND window, LPARAM parameter) -> BOOL {
        wchar_t className[40]{};
        if (!GetClassNameW(window, className, static_cast<int>(std::size(className))))
          return TRUE;
        bool secondary{};
        if (_wcsicmp(className, L"Shell_TrayWnd") == 0)
          secondary = false;
        else if (_wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0)
          secondary = true;
        else
          return TRUE;
        auto& value = *reinterpret_cast<Context*>(parameter);
        if (!CollectTaskbar(window, secondary, value.group, value.from, value.to, value.targets))
        {
          value.valid = false;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&context));
  if (!context.valid || context.targets.empty())
    return false;

  if (constructorUi && hasUiAdjustment &&
      std::none_of(context.targets.begin(), context.targets.end(),
                   [&](const TaskbarTarget& target) { return target.adjustedUi == constructorUi; }))
    return false;
  if (!constructorUi && !hasUiAdjustment)
    return false;

  std::vector<PointerMove> moves;
  moves.push_back({{groupItems->items, static_cast<size_t>(groupItems->count)},
                   static_cast<size_t>(groupFrom),
                   static_cast<size_t>(groupTo)});
  for (const auto& target : context.targets)
    moves.push_back({{target.buttons->items, static_cast<size_t>(target.buttons->count)},
                     static_cast<size_t>(target.fromIndex),
                     static_cast<size_t>(target.toIndex)});
  if (!ApplyPointerMoves(moves))
    return false;

  bool notified = true;
  if (constructorUi)
    notified = Notify(constructorUi, group, to);
  else
    for (const auto& target : context.targets)
      notified = Notify(target.adjustedUi, group, to) && notified;
  if (!notified)
  {
    RollbackPointerMoves(moves, moves.size());
    return false;
  }
  return true;
}

bool MoveTaskInGroup(void* group, void* from, void* to, void* constructorUi) noexcept
{
  bool ok = false;
  __try
  {
    ok = MoveTaskInGroupImpl(group, from, to, constructorUi);
  }
  __except (SehFilter(GetExceptionCode()))
  {
    ok = false;
  }
  if (!ok)
    LogEvent(20);
  return ok;
}

void SetFilterGate(bool value) noexcept
{
  filterGate.store(value, std::memory_order_release);
}

bool FilterGate() noexcept
{
  return filterGate.load(std::memory_order_acquire);
}

void CaptureDpa(void* pointer) noexcept
{
  if (captureThread.load(std::memory_order_acquire) == GetCurrentThreadId())
    capturedDpa = pointer;
}

bool ShouldCaptureDpa() noexcept
{
  return captureThread.load(std::memory_order_acquire) == GetCurrentThreadId();
}
} // namespace ttr::payload
