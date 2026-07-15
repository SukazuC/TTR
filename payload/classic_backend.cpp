#include "classic_backend.h"
#include "hook_transaction.h"
#include "payload_state.h"
#include "seh_filter.h"
#include "task_model.h"
#include <commctrl.h>
#include <cstdlib>
#include <windowsx.h>

namespace ttr::payload
{
namespace
{
HookTransaction hooks;
int dragged = -1;
bool dragDone = false;
bool dragThresholdMet = false;
POINT dragStart{};
ULONGLONG inclusionTick{};
using HoverFn = int(WINAPI*)(void*);
using ItemFn = void*(WINAPI*)(void*, int);
using GroupFn = void*(WINAPI*)(void*);
using ReorderedFn = void(WINAPI*)(void*, void*);
using WndFn = LRESULT(WINAPI*)(void*, HWND, UINT, WPARAM, LPARAM);
using FilterFn = bool(WINAPI*)(void*, void*);
using DpaFn = void*(WINAPI*)(HDPA, INT_PTR);
HoverFn hover{};
ItemFn item{};
GroupFn group{};
ReorderedFn reordered{};
WndFn wndOriginal{};
FilterFn filterOriginal{};
DpaFn dpaOriginal{};
using Active = CallbackScope;
void* Extended(void* p)
{
  return p ? static_cast<void**>(p) + 2 : nullptr;
}
bool Move(void* object, int from, int to)
{
  void *a = item(object, from), *b = item(object, to);
  if (!a || !b)
    return false;
  if (!MoveTaskInGroup(group(object), a, b))
    return false;
  reordered(object, a);
  inclusionTick = GetTickCount64();
  return true;
}
bool WINAPI FilterHook(void* self, void* task)
{
  Active active;
  if (GetState().enabled && FilterGate())
    return false;
  return filterOriginal(self, task);
}
void* WINAPI DpaHook(HDPA dpa, INT_PTR index)
{
  if (ShouldCaptureDpa())
    CaptureDpa(dpa);
  return dpaOriginal(dpa, index);
}
LRESULT WndLogic(void* self, HWND window, UINT message, WPARAM w, LPARAM l)
{
  if (!GetState().enabled || GetState().faulted)
    return wndOriginal(self, window, message, w, l);
  switch (message)
  {
  case WM_LBUTTONDOWN: {
    auto result = wndOriginal(self, window, message, w, l);
    auto* object = reinterpret_cast<void*>(GetWindowLongPtrW(window, 0));
    int at = hover(Extended(object));
    if (at >= 0)
    {
      dragged = at;
      dragDone = false;
      dragThresholdMet = false;
      dragStart = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      SetCapture(window);
    }
    return result;
  }
  case WM_MOUSEMOVE: {
    auto result = wndOriginal(self, window, message, w, l);
    if (GetCapture() == window && GetTickCount64() - inclusionTick > 60)
    {
      const POINT current{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      if (!dragThresholdMet && (std::abs(current.x - dragStart.x) >= GetSystemMetrics(SM_CXDRAG) ||
                                std::abs(current.y - dragStart.y) >= GetSystemMetrics(SM_CYDRAG)))
        dragThresholdMet = true;
      if (!dragThresholdMet)
        return result;
      auto* object = reinterpret_cast<void*>(GetWindowLongPtrW(window, 0));
      int at = hover(Extended(object));
      if (at >= 0 && at != dragged && Move(object, dragged, at))
      {
        dragged = at;
        dragDone = true;
      }
    }
    return result;
  }
  case WM_LBUTTONUP:
    if (GetCapture() == window)
    {
      ReleaseCapture();
      if (dragDone)
      {
        dragged = -1;
        dragDone = false;
        dragThresholdMet = false;
        return DefWindowProcW(window, message, w, l);
      }
    }
    dragged = -1;
    dragThresholdMet = false;
    return wndOriginal(self, window, message, w, l);
  case WM_CAPTURECHANGED:
    dragged = -1;
    dragDone = false;
    dragThresholdMet = false;
    return wndOriginal(self, window, message, w, l);
  case WM_CANCELMODE:
    if (GetCapture() == window)
      ReleaseCapture();
    dragged = -1;
    dragDone = false;
    dragThresholdMet = false;
    return wndOriginal(self, window, message, w, l);
  case WM_TIMER:
    if (w == 2006 && GetCapture() == window)
    {
      KillTimer(window, w);
      return DefWindowProcW(window, message, w, l);
    }
    break;
  }
  return wndOriginal(self, window, message, w, l);
}
LRESULT WndGuard(void* self, HWND window, UINT message, WPARAM w, LPARAM l)
{
  __try
  {
    return WndLogic(self, window, message, w, l);
  }
  __except (SehFilter(GetExceptionCode()))
  {
    MarkFault(PayloadError::StructuredException);
    return wndOriginal(self, window, message, w, l);
  }
}
LRESULT WINAPI WndHook(void* self, HWND window, UINT message, WPARAM w, LPARAM l)
{
  Active active;
  return WndGuard(self, window, message, w, l);
}
} // namespace
bool EnableClassic(const Compatibility& c) noexcept
{
  auto get = [&](SymbolId id) { return c.symbols[static_cast<size_t>(id)]; };
  hover = reinterpret_cast<HoverFn>(get(SymbolId::TaskListThumbnailWnd_GetHoverIndex));
  item = reinterpret_cast<ItemFn>(get(SymbolId::TaskListThumbnailWnd_GetTaskItem));
  group = reinterpret_cast<GroupFn>(get(SymbolId::TaskListThumbnailWnd_GetTaskGroup));
  reordered = reinterpret_cast<ReorderedFn>(get(SymbolId::TaskListThumbnailWnd_TaskReordered));
  void* wnd = get(SymbolId::TaskListThumbnailWnd_WndProc);
  void* filter = get(SymbolId::TaskItemFilter_IsTaskAllowed);
  if (!hover || !item || !group || !reordered || !wnd || !filter || !c.hasTaskListUiAdjustment ||
      !ConfigureTaskModel(c))
    return false;
  HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
  void* dpa = comctl ? reinterpret_cast<void*>(GetProcAddress(comctl, "DPA_GetPtr")) : nullptr;
  if (!dpa || !hooks.Begin())
    return false;
  if (!hooks.Add(wnd, reinterpret_cast<void*>(WndHook), reinterpret_cast<void**>(&wndOriginal)) ||
      !hooks.Add(filter, reinterpret_cast<void*>(FilterHook),
                 reinterpret_cast<void**>(&filterOriginal)) ||
      !hooks.Add(dpa, reinterpret_cast<void*>(DpaHook), reinterpret_cast<void**>(&dpaOriginal)) ||
      !hooks.Commit())
    return false;
  if (GetState().control)
    InterlockedExchange(&GetState().control->installedHookCount, static_cast<LONG>(hooks.count()));
  return true;
}
void DisableClassic() noexcept
{
  if (GetCapture())
  {
    wchar_t cls[40]{};
    GetClassNameW(GetCapture(), cls, 40);
    if (!_wcsicmp(cls, L"TaskListThumbnailWnd"))
      ReleaseCapture();
  }
  hooks.Reset();
  dragged = -1;
  dragDone = false;
  dragThresholdMet = false;
  if (GetState().control)
    InterlockedExchange(&GetState().control->installedHookCount, 0);
}
} // namespace ttr::payload
