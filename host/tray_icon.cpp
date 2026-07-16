#include "tray_icon.h"
#include <cwchar>
namespace ttr::host
{
namespace
{
HICON IconFor(TrayState s)
{
  int id = s == TrayState::Warning || s == TrayState::Faulted ? 203
           : s == TrayState::Active                           ? 201
                                                              : 202;
  return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), IMAGE_ICON,
                                       0, 0, LR_DEFAULTSIZE | LR_SHARED));
}
const wchar_t* TipFor(TrayState s)
{
  switch (s)
  {
  case TrayState::Active:
    return L"Taskbar Thumbnail Reorder - Active";
  case TrayState::Disabled:
    return L"Taskbar Thumbnail Reorder - Disabled";
  case TrayState::Warning:
    return L"Taskbar Thumbnail Reorder - Compatibility update required";
  default:
    return L"Taskbar Thumbnail Reorder - Faulted";
  }
}
} // namespace
bool TrayIcon::Add(HWND w, UINT m, TrayState s) noexcept
{
  data_ = {};
  data_.cbSize = sizeof(data_);
  data_.hWnd = w;
  data_.uID = 1;
  data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data_.uCallbackMessage = m;
  state_ = s;
  data_.hIcon = IconFor(s);
  wcscpy_s(data_.szTip, TipFor(s));
  if (!Shell_NotifyIconW(NIM_ADD, &data_))
    return false;
  data_.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &data_);
  return true;
}
void TrayIcon::Remove() noexcept
{
  if (data_.hWnd)
    Shell_NotifyIconW(NIM_DELETE, &data_);
  data_ = {};
}
void TrayIcon::SetState(TrayState s) noexcept
{
  state_ = s;
  if (!data_.hWnd)
    return;
  data_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data_.hIcon = IconFor(s);
  wcscpy_s(data_.szTip, TipFor(s));
  Shell_NotifyIconW(NIM_MODIFY, &data_);
}
} // namespace ttr::host
