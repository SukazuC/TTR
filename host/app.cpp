#include "app.h"
#include "autostart.h"
#include "cleanup.h"
#include "diagnostics.h"
#include "manifest_update.h"
#include "ttr_version.h"
#include <algorithm>
#include <new>
#include <shellapi.h>

namespace ttr::host
{
namespace
{
enum : UINT
{
  IdStatus = 100,
  IdEnable,
  IdAutostart,
  IdUpdate,
  IdDiagnostics,
  IdAbout,
  IdExit
};
constexpr UINT_PTR kRetryTimer = 1;
constexpr UINT_PTR kStableTimer = 2;
struct WaitContext
{
  App* app;
  std::uint64_t generation;
};
void UnregisterExplorerWait(HANDLE wait) noexcept
{
  if (!UnregisterWaitEx(wait, INVALID_HANDLE_VALUE))
  {
    const DWORD error = GetLastError();
    (void)error;
  }
}
} // namespace

int App::Run(HINSTANCE instance, int)
{
  instance_ = instance;
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
  settings_ = LoadSettings();
  std::string manifestError;
  if (!manifest_.Load(manifestError))
  {
    lastError_ = L"The compatibility manifest is corrupt.";
    state_ = HostState::Faulted;
  }
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  activateMessage_ = RegisterWindowMessageW(L"TaskbarThumbnailReorder.Activate.v1");
  replaceMessage_ = RegisterWindowMessageW(L"TaskbarThumbnailReorder.Replace.v1");
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance_;
  wc.lpszClassName = kWindowClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return 1;
  window_ = CreateWindowExW(0, kWindowClass, L"Taskbar Thumbnail Reorder", WS_OVERLAPPED, 0, 0, 0,
                            0, nullptr, nullptr, instance_, this);
  if (!window_)
    return 1;
  tray_.Add(window_, trayMessage_, TrayState::Disabled);
  if (state_ != HostState::Faulted)
  {
    if (settings_.enabled)
      AttachIfPossible();
    else
      SetState(HostState::Disabled);
  }
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (updateWorker_.joinable())
  {
    updateWorker_.request_stop();
    updateWorker_.join();
  }
  if (explorerWait_)
    UnregisterExplorerWait(explorerWait_);
  delete static_cast<WaitContext*>(waitContext_);
  waitContext_ = nullptr;
  loader_.Detach();
  CloseExplorerInfo(explorer_);
  tray_.Remove();
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
  if (message == WM_NCCREATE)
  {
    auto* self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    return TRUE;
  }
  auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  return self ? self->HandleMessage(message, wParam, lParam)
              : DefWindowProcW(window, message, wParam, lParam);
}
void CALLBACK App::ExplorerExited(void* context, BOOLEAN) noexcept
{
  auto* wait = static_cast<WaitContext*>(context);
  if (wait && wait->app && wait->app->window_)
    PostMessageW(wait->app->window_, WM_APP + 2, 0, static_cast<LPARAM>(wait->generation));
}

LRESULT App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
  if (message == replaceMessage_)
  {
    if (loader_.Detach())
    {
      SetState(HostState::Exiting);
      DestroyWindow(window_);
    }
    else
      SetState(HostState::Faulted);
    return 0;
  }
  if (message == WM_APP + 3)
  {
    updateInProgress_ = false;
    if (updateWorker_.joinable())
      updateWorker_.join();
    const auto result = static_cast<ManifestUpdateResult>(wParam);
    const wchar_t* text = result == ManifestUpdateResult::Installed
                              ? L"A verified compatibility manifest was installed. Toggle the "
                                L"feature off and on to apply it."
                          : result == ManifestUpdateResult::NotConfigured
                              ? L"No compatibility update endpoint is configured in this build."
                          : result == ManifestUpdateResult::Cancelled
                              ? L"The compatibility update was cancelled."
                              : L"No newer valid signed compatibility update could be installed.";
    MessageBoxW(
        window_, text, L"Taskbar Thumbnail Reorder",
        MB_OK | (result == ManifestUpdateResult::Installed ? MB_ICONINFORMATION : MB_ICONWARNING));
    return 0;
  }
  if (message == WM_APP + 2)
  {
    if (static_cast<std::uint64_t>(lParam) != explorerGeneration_)
      return 0;
    if (explorerWait_)
      UnregisterExplorerWait(explorerWait_);
    explorerWait_ = nullptr;
    delete static_cast<WaitContext*>(waitContext_);
    waitContext_ = nullptr;
    loader_.Detach();
    CloseExplorerInfo(explorer_);
    KillTimer(window_, kStableTimer);
    const ULONGLONG now = GetTickCount64();
    if (!crashWindowStart_ || now - crashWindowStart_ > 60000)
    {
      crashWindowStart_ = now;
      crashCount_ = 0;
    }
    ++crashCount_;
    SetState(HostState::ExplorerUnavailable);
    if (settings_.enabled)
    {
      if (crashCount_ >= 5)
      {
        lastError_ = L"Explorer restarted repeatedly; automatic attachment is suppressed until the "
                     L"feature is toggled.";
        SetState(HostState::Faulted);
      }
      else
        ScheduleExplorerRetry();
    }
    return 0;
  }
  if (message == taskbarCreated_)
  {
    tray_.Remove();
    tray_.Add(window_, trayMessage_,
              state_ == HostState::Active        ? TrayState::Active
              : state_ == HostState::Unsupported ? TrayState::Warning
                                                 : TrayState::Disabled);
    if (explorerWait_)
      UnregisterExplorerWait(explorerWait_);
    explorerWait_ = nullptr;
    delete static_cast<WaitContext*>(waitContext_);
    waitContext_ = nullptr;
    ++explorerGeneration_;
    loader_.Detach();
    CloseExplorerInfo(explorer_);
    if (settings_.enabled)
      AttachIfPossible();
    return 0;
  }
  if (message == activateMessage_)
  {
    POINT point{};
    GetCursorPos(&point);
    ShowMenu(point);
    return 0;
  }
  if (message == trayMessage_)
  {
    auto event = LOWORD(lParam);
    if (event == WM_CONTEXTMENU || event == NIN_SELECT || event == WM_LBUTTONUP)
    {
      POINT point{};
      GetCursorPos(&point);
      ShowMenu(point);
    }
    else if (event == WM_LBUTTONDBLCLK)
      ToggleEnabled();
    return 0;
  }
  switch (message)
  {
  case WM_COMMAND:
    switch (LOWORD(wParam))
    {
    case IdEnable:
      ToggleEnabled();
      break;
    case IdAutostart: {
      std::wstring error;
      bool ok = SetAutostartEnabled(!IsAutostartEnabled(), error);
      if (!ok && !error.empty())
        MessageBoxW(window_, error.c_str(), L"Taskbar Thumbnail Reorder", MB_OK | MB_ICONERROR);
      break;
    }
    case IdUpdate:
      if (!updateInProgress_)
      {
        if (updateWorker_.joinable())
          updateWorker_.join();
        updateInProgress_ = true;
        lastError_ = L"Compatibility update is in progress.";
        updateWorker_ = std::jthread(RunCompatibilityUpdate, window_);
      }
      break;
    case IdDiagnostics: {
      std::wstring error;
      ExportDiagnostics(window_, explorer_.processId ? &explorer_ : nullptr, recordId_,
                        StateText() + L"\r\nLast error: " + lastError_, error);
      break;
    }
    case IdAbout: {
      const auto about =
          std::wstring(L"Taskbar Thumbnail Reorder ") + TTR_VERSION_W +
          L"\n\nGPL-3.0-only. Based on the Windhawk mod by Michael Maltsev (m417z).\nUnknown "
          L"taskbar builds are never modified.\n\nRemove this application from this PC?";
      if (MessageBoxW(window_, about.c_str(), L"About",
                      MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) == IDYES)
      {
        std::wstring error;
        if (!loader_.Detach())
        {
          SetState(HostState::Faulted);
          MessageBoxW(window_,
                      L"Explorer callbacks are still active. Removal was cancelled to avoid "
                      L"unloading live code.",
                      L"Taskbar Thumbnail Reorder", MB_OK | MB_ICONWARNING);
          break;
        }
        SetAutostartEnabled(false, error);
        if (LaunchCleanupHelper(GetCurrentProcessId(), ApplicationDataDirectory(), error))
        {
          SetState(HostState::Exiting);
          DestroyWindow(window_);
        }
        else
          MessageBoxW(window_, error.c_str(), L"Taskbar Thumbnail Reorder", MB_OK | MB_ICONERROR);
      }
      break;
    }
    case IdExit:
      if (loader_.Detach())
      {
        SetState(HostState::Exiting);
        DestroyWindow(window_);
      }
      else
      {
        SetState(HostState::Faulted);
        MessageBoxW(window_,
                    L"Explorer callbacks are still active. Exit was cancelled to avoid unloading "
                    L"live code.",
                    L"Taskbar Thumbnail Reorder", MB_OK | MB_ICONWARNING);
      }
      break;
    }
    return 0;
  case WM_TIMER:
    if (wParam == kRetryTimer)
    {
      KillTimer(window_, kRetryTimer);
      AttachIfPossible();
    }
    else if (wParam == kStableTimer)
    {
      KillTimer(window_, kStableTimer);
      retryAttempt_ = 0;
      crashCount_ = 0;
      crashWindowStart_ = 0;
    }
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window_, message, wParam, lParam);
}

void App::SetState(HostState value) noexcept
{
  state_ = value;
  TrayState trayState = TrayState::Disabled;
  if (value == HostState::Active)
    trayState = TrayState::Active;
  else if (value == HostState::Unsupported)
    trayState = TrayState::Warning;
  else if (value == HostState::Faulted)
    trayState = TrayState::Faulted;
  tray_.SetState(trayState);
}
std::wstring App::StateText() const
{
  switch (state_)
  {
  case HostState::Active:
    return L"Active";
  case HostState::Disabled:
    return L"Disabled";
  case HostState::Unsupported:
    return L"Compatibility update required";
  case HostState::ExplorerUnavailable:
    return L"Explorer unavailable";
  case HostState::Faulted:
    return L"Faulted";
  case HostState::Attaching:
    return L"Attaching";
  default:
    return L"Starting";
  }
}
void App::AttachIfPossible() noexcept
{
  if (explorerWait_)
  {
    UnregisterExplorerWait(explorerWait_);
    explorerWait_ = nullptr;
  }
  delete static_cast<WaitContext*>(waitContext_);
  waitContext_ = nullptr;
  ++explorerGeneration_;
  loader_.Detach();
  CloseExplorerInfo(explorer_);
  SetState(HostState::ValidatingCompatibility);
  if (!FindAndValidateExplorer(explorer_, lastError_))
  {
    SetState(HostState::ExplorerUnavailable);
    ScheduleExplorerRetry();
    return;
  }
  auto* context = new (std::nothrow) WaitContext;
  if (context)
  {
    context->app = this;
    context->generation = explorerGeneration_;
    if (RegisterWaitForSingleObject(&explorerWait_, explorer_.process, ExplorerExited, context,
                                    INFINITE, WT_EXECUTEONLYONCE))
      waitContext_ = context;
    else
      delete context;
  }
  auto blob = manifest_.SelectedRecordBlob(explorer_.modules, recordId_);
  if (blob.empty())
  {
    lastError_ = L"Compatibility update required for this Windows taskbar version. No changes were "
                 L"made to Explorer.";
    SetState(HostState::Unsupported);
    return;
  }
  SetState(HostState::Attaching);
  if (!loader_.Attach(explorer_, blob, lastError_))
  {
    loader_.Detach();
    SetState(HostState::Faulted);
    return;
  }
  std::wstring error;
  if (!loader_.CommandAndWait(Command::Enable, 0, 1000, error))
  {
    lastError_ = error;
    if (!loader_.Detach())
      lastError_ += L" The attached payload could not be safely detached.";
    SetState(HostState::Faulted);
    return;
  }
  SetState(HostState::Active);
  SetTimer(window_, kStableTimer, 30000, nullptr);
}
void App::ScheduleExplorerRetry() noexcept
{
  if (!settings_.enabled)
    return;
  if (retryAttempt_ >= 8)
  {
    lastError_ =
        L"Explorer remained unavailable after bounded retries. Toggle the feature to retry.";
    SetState(HostState::Faulted);
    return;
  }
  const UINT delay = std::min<UINT>(30000u, 250u * (1u << retryAttempt_));
  ++retryAttempt_;
  SetTimer(window_, kRetryTimer, delay, nullptr);
}
void App::ToggleEnabled() noexcept
{
  settings_.enabled = !settings_.enabled;
  SaveEnabled(settings_.enabled);
  KillTimer(window_, kRetryTimer);
  KillTimer(window_, kStableTimer);
  retryAttempt_ = 0;
  crashCount_ = 0;
  crashWindowStart_ = 0;
  if (settings_.enabled)
    AttachIfPossible();
  else if (loader_.Detach())
    SetState(HostState::Disabled);
  else
    SetState(HostState::Faulted);
}
void App::ShowMenu(POINT point) noexcept
{
  HMENU menu = CreatePopupMenu();
  auto status = L"Status: " + StateText();
  AppendMenuW(menu, MF_STRING | MF_DISABLED, IdStatus, status.c_str());
  AppendMenuW(menu,
              MF_STRING | (settings_.enabled ? MF_CHECKED : 0) |
                  (state_ == HostState::Unsupported ? MF_DISABLED : 0),
              IdEnable, L"Enable thumbnail reordering");
  AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), IdAutostart,
              L"Start with Windows");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IdUpdate, L"Check compatibility update");
  AppendMenuW(menu, MF_STRING, IdDiagnostics, L"Export diagnostics...");
  AppendMenuW(menu, MF_STRING, IdAbout, L"About");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IdExit, L"Exit");
  SetForegroundWindow(window_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, window_, nullptr);
  DestroyMenu(menu);
  PostMessageW(window_, WM_NULL, 0, 0);
}
} // namespace ttr::host
