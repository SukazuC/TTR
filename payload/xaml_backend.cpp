#include "xaml_backend.h"
#include "hook_transaction.h"
#include "payload_log.h"
#include "payload_state.h"
#include "seh_filter.h"
#include "task_model.h"
#include "thumbnail_context.h"
#include "winrt_visual_tree.h"
#include <Windows.h>
#include <commctrl.h>
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>

namespace ttr::payload
{
namespace
{
using namespace winrt;
using namespace Windows::UI::Xaml;

struct Mapping
{
  weak_ref<Windows::Foundation::IInspectable> thumbnail;
  ThumbnailContext context;
  std::uint64_t order{};
};
std::vector<Mapping> mappings;
weak_ref<Windows::Foundation::IInspectable> currentThumbnails;
HookTransaction hooks;
bool resolvingTarget{};
std::atomic_bool reordered{};
bool animated{};
std::uint64_t mappingOrder{};
std::uint64_t mappingGeneration{1};

using Constructor1Fn = void*(WINAPI*)(void*, void*, void*, void*, void*, void*, void*, bool);
using Constructor2Fn = void*(WINAPI*)(void*, void*, void*, void*, void*, void*, bool);
using ThumbnailsFn = void*(WINAPI*)(void*, void*);
using SizeFn = int(WINAPI*)(void*);
using GetAtFn = void*(WINAPI*)(void*, void**, int);
using ClickFn = HRESULT(WINAPI*)(void*, void*, void*, void*);
using TargetFn = void(WINAPI*)(void*, void*);
using PointerFn = int(WINAPI*)(void*, void*);
using PositionFn = void(WINAPI*)(void*);
using FilterFn = bool(WINAPI*)(void*, void*);
using DpaFn = void*(WINAPI*)(HDPA, INT_PTR);
Constructor1Fn constructor1Original{};
Constructor2Fn constructor2Original{};
ThumbnailsFn thumbnailsOriginal{};
SizeFn sizeOriginal{};
GetAtFn getAtOriginal{};
ClickFn clickOriginal{};
TargetFn targetOriginal{};
PointerFn pointerOriginal{};
PositionFn positionOriginal{};
FilterFn filterOriginal{};
DpaFn dpaOriginal{};

using Active = CallbackScope;

FrameworkElement Children(FrameworkElement const& element,
                          bool (*callback)(FrameworkElement const&, void*), void* context)
{
  const int count = Media::VisualTreeHelper::GetChildrenCount(element);
  for (int i = 0; i < count; ++i)
  {
    auto child = Media::VisualTreeHelper::GetChild(element, i).try_as<FrameworkElement>();
    if (child && callback(child, context))
      return child;
  }
  return nullptr;
}
FrameworkElement ByName(FrameworkElement const& element, wchar_t const* name)
{
  struct C
  {
    wchar_t const* n;
  };
  C c{name};
  return Children(
      element, [](FrameworkElement const& e, void* p) { return e.Name() == static_cast<C*>(p)->n; },
      &c);
}
FrameworkElement ByClass(FrameworkElement const& element, wchar_t const* name)
{
  struct C
  {
    wchar_t const* n;
  };
  C c{name};
  return Children(
      element,
      [](FrameworkElement const& e, void* p) { return get_class_name(e) == static_cast<C*>(p)->n; },
      &c);
}

void Prune()
{
  std::erase_if(mappings, [](auto const& m) { return !m.thumbnail.get(); });
  if (mappings.size() > kMaximumThumbnailMappings)
  {
    std::sort(mappings.begin(), mappings.end(),
              [](auto const& a, auto const& b) { return a.order < b.order; });
    mappings.erase(mappings.begin(),
                   mappings.begin() +
                       static_cast<std::ptrdiff_t>(mappings.size() - kMaximumThumbnailMappings));
  }
}
void AddMapping(Windows::Foundation::IInspectable const& thumbnail, void* group, void* item,
                void* taskListUi)
{
  if (!thumbnail || !group || !item || !taskListUi || !mappingGeneration)
    return;
  std::erase_if(mappings, [&](auto const& m) {
    auto current = m.thumbnail.get();
    return !current || current == thumbnail ||
           (m.context.taskGroup == group && m.context.taskItem == item);
  });
  mappings.push_back({thumbnail, {group, item, taskListUi, mappingGeneration}, ++mappingOrder});
  Prune();
}
void CaptureConstructed(void* result, void* group, void* item, void* taskListUi)
{
  if (!result)
    return;
  Windows::Foundation::IInspectable object{nullptr};
  auto* adjusted = reinterpret_cast<IUnknown*>(result) + 2;
  adjusted->QueryInterface(guid_of<Windows::Foundation::IInspectable>(), put_abi(object));
  if (object)
    AddMapping(object, group, item, taskListUi);
}

void* WINAPI Constructor1Hook(void* a, void* b, void* group, void* item, void* ui, void* f,
                              void* accessibility, bool flag)
{
  Active active;
  void* result = constructor1Original(a, b, group, item, ui, f, accessibility, flag);
  if (GetState().enabled)
    try
    {
      CaptureConstructed(result, group, item, ui);
    }
    catch (...)
    {
      LogEvent(40);
    }
  return result;
}
void* WINAPI Constructor2Hook(void* a, void* b, void* group, void* item, void* ui, void* f,
                              bool flag)
{
  Active active;
  void* result = constructor2Original(a, b, group, item, ui, f, flag);
  if (GetState().enabled)
    try
    {
      CaptureConstructed(result, group, item, ui);
    }
    catch (...)
    {
      LogEvent(41);
    }
  return result;
}
void* WINAPI ThumbnailsHook(void* self, void* arg)
{
  Active active;
  void* result = thumbnailsOriginal(self, arg);
  if (GetState().enabled && resolvingTarget && result)
    try
    {
      Windows::Foundation::IInspectable object{nullptr};
      (*reinterpret_cast<IUnknown**>(result))
          ->QueryInterface(guid_of<Windows::Foundation::IInspectable>(), put_abi(object));
      if (object)
      {
        auto previous = currentThumbnails.get();
        if (!previous || previous != object)
        {
          currentThumbnails = object;
          Prune();
        }
      }
    }
    catch (...)
    {
      LogEvent(42);
    }
  return result;
}
HRESULT WINAPI ClickHook(void* self, void* group, void* item, void* options)
{
  Active active;
  if (GetState().enabled && reordered.exchange(false))
    return S_OK;
  return clickOriginal(self, group, item, options);
}
void WINAPI TargetHook(void* self, void* key)
{
  Active active;
  resolvingTarget = true;
  targetOriginal(self, key);
  resolvingTarget = false;
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

bool Inside(UIElement const& element, Input::PointerRoutedEventArgs const& args)
{
  auto point = args.GetCurrentPoint(element).Position();
  auto size = element.RenderSize();
  return point.X >= 0 && point.Y >= 0 && point.X < size.Width && point.Y < size.Height;
}
bool MoveXaml(int from, int to)
{
  if (animated && std::abs(from - to) != 1)
  {
    LogEvent(43, from, to);
    return false;
  }
  auto thumbnails = currentThumbnails.get();
  if (!thumbnails)
    return false;
  auto* raw = get_abi(thumbnails);
  int count = sizeOriginal(&raw);
  if (count < 0 || count > 1024 || from < 0 || to < 0 || from >= count || to >= count)
    return false;
  com_ptr<IUnknown> fromObject, toObject;
  getAtOriginal(&raw, fromObject.put_void(), from);
  getAtOriginal(&raw, toObject.put_void(), to);
  ThumbnailContext fromContext{};
  ThumbnailContext toContext{};
  for (auto const& m : mappings)
  {
    auto thumbnail = m.thumbnail.get();
    if (!thumbnail)
      continue;
    auto* abi = get_abi(thumbnail);
    if (abi == fromObject.get())
      fromContext = m.context;
    if (abi == toObject.get())
      toContext = m.context;
  }
  if (!SameReorderContext(fromContext, toContext, mappingGeneration))
    return false;
  return MoveTaskInGroup(fromContext.taskGroup, fromContext.taskItem, toContext.taskItem,
                         fromContext.taskListUi);
}

int PointerLogic(void* self, void* rawArgs)
{
  if (!GetState().enabled || !GetCapture())
  {
    reordered = false;
    return pointerOriginal(self, rawArgs);
  }
  FrameworkElement root{nullptr};
  reinterpret_cast<IUnknown*>(self)->QueryInterface(guid_of<FrameworkElement>(), put_abi(root));
  if (!root)
    return pointerOriginal(self, rawArgs);
  FrameworkElement repeaterElement{nullptr};
  auto className = get_class_name(root);
  if (className == L"Taskbar.TaskItemThumbnailList")
    repeaterElement = ByName(root, L"TaskItemThumbnailListRepeater");
  else if (className == L"Taskbar.TaskItemThumbnailScrollableList")
  {
    auto child = ByName(root, L"TaskItemThumbnailScrollableListScrollViewer");
    if (child)
      child = ByName(child, L"Root");
    if (child)
      child = ByClass(child, L"Windows.UI.Xaml.Controls.Grid");
    if (child)
      child = ByName(child, L"ScrollContentPresenter");
    if (child)
      repeaterElement = ByName(child, L"TaskItemThumbnailListRepeater");
  }
  if (!repeaterElement)
    return pointerOriginal(self, rawArgs);
  auto repeater = repeaterElement.try_as<winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater>();
  if (!repeater)
    return pointerOriginal(self, rawArgs);
  Input::PointerRoutedEventArgs args{nullptr};
  reinterpret_cast<IUnknown*>(rawArgs)->QueryInterface(guid_of<Input::PointerRoutedEventArgs>(),
                                                       put_abi(args));
  if (!args)
    return pointerOriginal(self, rawArgs);
  int pressed = -1, hovered = -1;
  auto view = repeater.ItemsSourceView();
  int count = view ? view.Count() : 0;
  if (count < 0 || count > 256)
    return pointerOriginal(self, rawArgs);
  for (int index = 0; index < count; ++index)
  {
    auto element = repeater.TryGetElement(index);
    if (!element)
      continue;
    auto child = element.try_as<FrameworkElement>();
    if (!child || get_class_name(child) != L"Taskbar.TaskItemThumbnailView")
      continue;
    auto grid = ByClass(child, L"Windows.UI.Xaml.Controls.Grid");
    if (!grid)
      continue;
    if (pressed < 0)
    {
      for (auto const& group : VisualStateManager::GetVisualStateGroups(grid))
      {
        if (group.Name() != L"CommonStates")
          continue;
        auto state = group.CurrentState();
        if (state && (state.Name() == L"Pressed" || state.Name() == L"RequestingAttentionPressed"))
          pressed = index;
        break;
      }
    }
    if (hovered < 0 && Inside(child, args))
      hovered = index;
    if (pressed >= 0 && hovered >= 0)
      break;
  }
  if (pressed >= 0 && hovered >= 0 && pressed != hovered && MoveXaml(pressed, hovered))
    reordered = true;
  return pointerOriginal(self, rawArgs);
}
int PointerCaught(void* self, void* args)
{
  try
  {
    return PointerLogic(self, args);
  }
  catch (...)
  {
    LogEvent(44);
    return pointerOriginal(self, args);
  }
}
int PointerGuard(void* self, void* args)
{
  __try
  {
    return PointerCaught(self, args);
  }
  __except (SehFilter(GetExceptionCode()))
  {
    MarkFault(PayloadError::StructuredException);
    return pointerOriginal(self, args);
  }
}
int WINAPI PointerHook(void* self, void* args)
{
  Active active;
  return PointerGuard(self, args);
}
void WINAPI PositionHook(void* self)
{
  Active active;
  if (!GetState().enabled)
  {
    positionOriginal(self);
    return;
  }
  if (!animated && reordered)
  {
    if (GetCapture())
      return;
    reordered = false;
  }
  positionOriginal(self);
}
} // namespace

bool EnableXaml(const Compatibility& c) noexcept
{
  auto get = [&](SymbolId id) { return c.symbols[static_cast<size_t>(id)]; };
  void *ctor1 = get(SymbolId::TaskItemThumbnail_ConstructorV1),
       *ctor2 = get(SymbolId::TaskItemThumbnail_ConstructorV2);
  void *thumbs = get(SymbolId::TaskGroup_Thumbnails),
       *size = get(SymbolId::TaskItemThumbnailVector_Size),
       *at = get(SymbolId::TaskItemThumbnailVector_GetAt);
  void *click = get(SymbolId::TaskListWnd_HandleExtendedUIClick),
       *target = get(SymbolId::HoverFlyoutModel_TargetItemKey);
  void *pointer = get(SymbolId::TaskItemThumbnailList_OnPointerMoved),
       *position = get(SymbolId::FlyoutFrame_UpdateFlyoutPosition);
  void* filter = get(SymbolId::TaskItemFilter_IsTaskAllowed);
  HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
  void* dpa = comctl ? reinterpret_cast<void*>(GetProcAddress(comctl, "DPA_GetPtr")) : nullptr;
  if ((!ctor1 && !ctor2) || !thumbs || !size || !at || !click || !target || !pointer || !position ||
      !filter || !dpa || !ConfigureTaskModel(c) || !hooks.Begin())
    return false;
  bool ok = true;
  if (ctor1)
    ok = hooks.Add(ctor1, reinterpret_cast<void*>(Constructor1Hook),
                   reinterpret_cast<void**>(&constructor1Original));
  if (ok && ctor2)
    ok = hooks.Add(ctor2, reinterpret_cast<void*>(Constructor2Hook),
                   reinterpret_cast<void**>(&constructor2Original));
  if (ok)
    ok = hooks.Add(thumbs, reinterpret_cast<void*>(ThumbnailsHook),
                   reinterpret_cast<void**>(&thumbnailsOriginal));
  sizeOriginal = reinterpret_cast<SizeFn>(size);
  getAtOriginal = reinterpret_cast<GetAtFn>(at);
  if (ok)
    ok = hooks.Add(click, reinterpret_cast<void*>(ClickHook),
                   reinterpret_cast<void**>(&clickOriginal));
  if (ok)
    ok = hooks.Add(target, reinterpret_cast<void*>(TargetHook),
                   reinterpret_cast<void**>(&targetOriginal));
  if (ok)
    ok = hooks.Add(pointer, reinterpret_cast<void*>(PointerHook),
                   reinterpret_cast<void**>(&pointerOriginal));
  if (ok)
    ok = hooks.Add(position, reinterpret_cast<void*>(PositionHook),
                   reinterpret_cast<void**>(&positionOriginal));
  if (ok)
    ok = hooks.Add(filter, reinterpret_cast<void*>(FilterHook),
                   reinterpret_cast<void**>(&filterOriginal));
  if (ok)
    ok = hooks.Add(dpa, reinterpret_cast<void*>(DpaHook), reinterpret_cast<void**>(&dpaOriginal));
  if (!ok || !hooks.Commit())
  {
    hooks.Reset();
    return false;
  }
  animated = (c.backendFlags & BackendAnimatedXaml) != 0;
  if (GetState().control)
    InterlockedExchange(&GetState().control->installedHookCount, static_cast<LONG>(hooks.count()));
  return true;
}
void DisableXaml() noexcept
{
  hooks.Reset();
  mappings.clear();
  ++mappingGeneration;
  if (!mappingGeneration)
    mappingGeneration = 1;
  mappingOrder = 0;
  currentThumbnails = {};
  resolvingTarget = false;
  reordered = false;
  if (GetState().control)
    InterlockedExchange(&GetState().control->installedHookCount, 0);
}
} // namespace ttr::payload
