#include "task_model.h"
#include "payload_log.h"
#include "seh_filter.h"
#include "ttr_array_move.h"
#include <atomic>
#include <cstring>

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
using GetNumItemsFn = int(WINAPI*)(void*);
using GetGroupFn = void*(WINAPI*)(void*, void*, int*);
using GetTypeFn = int(WINAPI*)(void*);
using IndexFn = int(WINAPI*)(void*, void*);
using ChangedFn = HRESULT(WINAPI*)(void*, void*, void*);
void* vtable{};
GetNumItemsFn getNum{};
GetGroupFn getGroup{};
GetTypeFn getType{};
IndexFn indexOf{};
ChangedFn changed{};
std::atomic_bool filterGate{};
std::atomic<DWORD> captureThread{};
void* capturedDpa{};
ULONGLONG lastRefresh{};
void* FindInterface(void* object, void* table)
{
  if (!object || !table)
    return nullptr;
  auto** p = static_cast<void**>(object);
  for (size_t i = 0; i < 64; ++i)
    if (p[i] == table)
      return p + i;
  return nullptr;
}
int CaptureIndex(void* buttonGroup, void* item, Dpa*& array)
{
  capturedDpa = nullptr;
  captureThread = GetCurrentThreadId();
  int result = -1;
  __try
  {
    result = indexOf(buttonGroup, item);
  }
  __finally
  {
    captureThread = 0;
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
  if (!dpa || dpa->count < 0 || dpa->count > 1024 || dpa->capacity < dpa->count ||
      dpa->capacity > 4096 || (dpa->count && !dpa->items))
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
bool MoveTaskbar(HWND tray, bool secondary, void* group, void* from, void* to)
{
  HWND switcher = secondary ? FindWindowExW(tray, nullptr, L"WorkerW", nullptr)
                            : reinterpret_cast<HWND>(GetPropW(tray, L"TaskbandHWND"));
  if (!switcher)
    return false;
  HWND list = FindWindowExW(switcher, nullptr, L"MSTaskListWClass", nullptr);
  if (!list)
    return false;
  void* object = reinterpret_cast<void*>(GetWindowLongPtrW(list, 0));
  void* buttonGroup = getGroup(object, group, nullptr);
  if (!buttonGroup)
    return false;
  int type = getType(buttonGroup);
  if (type != 1 && type != 3)
    return false;
  Dpa* buttons{};
  int fromIndex = CaptureIndex(buttonGroup, from, buttons);
  if (fromIndex < 0 || !buttons || buttons->count < 0 || buttons->count > 1024 ||
      buttons->capacity < buttons->count || buttons->capacity > 4096 ||
      (buttons->count && !buttons->items))
    return false;
  Dpa* second{};
  int toIndex = CaptureIndex(buttonGroup, to, second);
  if (toIndex < 0 || second != buttons || fromIndex >= buttons->count || toIndex >= buttons->count)
    return false;
  if (!ttr::MovePointer({buttons->items, static_cast<size_t>(buttons->count)},
                        static_cast<size_t>(fromIndex), static_cast<size_t>(toIndex)))
    return false;
  void* ui = FindInterface(object, vtable);
  if (!ui)
    return false;
  HWND captured = GetCapture();
  if (captured)
    SendMessageW(captured, WM_SETREDRAW, FALSE, 0);
  __try
  {
    filterGate = true;
    changed(ui, group, to);
    filterGate = false;
    changed(ui, group, to);
    lastRefresh = GetTickCount64();
  }
  __finally
  {
    filterGate = false;
    if (captured)
    {
      SendMessageW(captured, WM_SETREDRAW, TRUE, 0);
      RedrawWindow(captured, nullptr, nullptr,
                   RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
  }
  return true;
}
} // namespace
bool ConfigureTaskModel(const Compatibility& c) noexcept
{
  auto get = [&](SymbolId id) { return c.symbols[static_cast<size_t>(id)]; };
  vtable = get(SymbolId::TaskListWnd_Vtable_ITaskListUI);
  getNum = reinterpret_cast<GetNumItemsFn>(get(SymbolId::TaskGroup_GetNumItems));
  getGroup = reinterpret_cast<GetGroupFn>(get(SymbolId::TaskListWnd_GetTBGroupFromGroup));
  getType = reinterpret_cast<GetTypeFn>(get(SymbolId::TaskBtnGroup_GetGroupType));
  indexOf = reinterpret_cast<IndexFn>(get(SymbolId::TaskBtnGroup_IndexOfTaskItem));
  changed = reinterpret_cast<ChangedFn>(get(SymbolId::TaskListWnd_TaskInclusionChanged));
  return vtable && getNum && getGroup && getType && indexOf && changed;
}
bool MoveTaskInGroup(void* group, void* from, void* to) noexcept
{
  bool ok = false;
  __try
  {
    auto* dpa = TaskItems(group);
    if (!dpa)
      return false;
    int a = -1, b = -1;
    for (int i = 0; i < dpa->count; ++i)
    {
      if (dpa->items[i] == from)
        a = i;
      if (dpa->items[i] == to)
        b = i;
    }
    if (a < 0 || b < 0 || a == b)
      return false;
    if (!ttr::MovePointer({dpa->items, static_cast<size_t>(dpa->count)}, static_cast<size_t>(a),
                          static_cast<size_t>(b)))
      return false;
    struct Ctx
    {
      void* g;
      void* f;
      void* t;
      bool ok;
    } ctx{group, from, to, false};
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND w, LPARAM p) -> BOOL {
          wchar_t cls[40]{};
          GetClassNameW(w, cls, 40);
          bool secondary = false;
          if (_wcsicmp(cls, L"Shell_TrayWnd") && _wcsicmp(cls, L"Shell_SecondaryTrayWnd"))
            return TRUE;
          if (!_wcsicmp(cls, L"Shell_SecondaryTrayWnd"))
            secondary = true;
          auto& c = *reinterpret_cast<Ctx*>(p);
          c.ok = MoveTaskbar(w, secondary, c.g, c.f, c.t) || c.ok;
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.ok)
      ttr::MovePointer({dpa->items, static_cast<size_t>(dpa->count)}, static_cast<size_t>(b),
                       static_cast<size_t>(a));
    ok = ctx.ok;
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
  filterGate = value;
}
bool FilterGate() noexcept
{
  return filterGate.load();
}
void CaptureDpa(void* p) noexcept
{
  if (captureThread == GetCurrentThreadId())
    capturedDpa = p;
}
bool ShouldCaptureDpa() noexcept
{
  return captureThread == GetCurrentThreadId();
}
} // namespace ttr::payload
