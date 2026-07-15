#pragma once
#include "explorer_session.h"
#include "ttr_protocol.h"
#include <Windows.h>
#include <span>
#include <string>

namespace ttr::host {
class HookLoader {
 public:
  ~HookLoader();
  bool Attach(const ExplorerInfo&, std::span<const std::byte>, std::wstring&) noexcept;
  bool CommandAndWait(Command, LONG, DWORD, std::wstring&) noexcept;
  bool Detach() noexcept;
  PayloadState state() const noexcept;
 private:
  bool ExtractPayload(std::wstring&, std::wstring&) noexcept;
  HMODULE localModule_{}; HHOOK hook_{}; HANDLE mapping_{}; void* mappingView_{};
  TtrSessionControlV1* control_{}; HWND taskbar_{}; DWORD threadId_{};DWORD explorerPid_{}; std::wstring payloadPath_;
};
}
