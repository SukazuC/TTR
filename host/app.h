#pragma once
#include "explorer_session.h"
#include "hook_loader.h"
#include "manifest_store.h"
#include "settings.h"
#include "tray_icon.h"
#include <Windows.h>
#include <cstdint>
#include <string>

namespace ttr::host {
enum class HostState { Starting,WaitingForExplorer,ValidatingCompatibility,Attaching,Active,Disabled,Unsupported,ExplorerUnavailable,Faulted,Exiting };
class App {
 public:
  int Run(HINSTANCE, int);
  static constexpr wchar_t kWindowClass[]=L"TaskbarThumbnailReorder.Host.v1";
 private:
  static LRESULT CALLBACK WindowProc(HWND,UINT,WPARAM,LPARAM) noexcept;
  static void CALLBACK ExplorerExited(void*,BOOLEAN) noexcept;
  LRESULT HandleMessage(UINT,WPARAM,LPARAM) noexcept;
  void ShowMenu(POINT) noexcept; void ToggleEnabled() noexcept; void AttachIfPossible() noexcept; void SetState(HostState) noexcept;
  std::wstring StateText()const;
  HWND window_{}; HINSTANCE instance_{}; UINT trayMessage_{WM_APP+1};UINT taskbarCreated_{};UINT activateMessage_{};UINT replaceMessage_{};
  Settings settings_{};TrayIcon tray_;HostState state_{HostState::Starting};ManifestStore manifest_;ExplorerInfo explorer_;HookLoader loader_;HANDLE explorerWait_{};void*waitContext_{};std::uint64_t explorerGeneration_{};std::uint64_t recordId_{};std::wstring lastError_;
};
}
