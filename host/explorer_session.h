#pragma once
#include "pe_identity.h"
#include <Windows.h>
#include <string>
#include <vector>
namespace ttr::host
{
struct ExplorerInfo
{
  HWND taskbar{};
  DWORD threadId{};
  DWORD processId{};
  HANDLE process{};
  std::wstring path;
  std::vector<PeIdentity> modules;
};
bool FindAndValidateExplorer(ExplorerInfo&, std::wstring&) noexcept;
void CloseExplorerInfo(ExplorerInfo&) noexcept;
} // namespace ttr::host
