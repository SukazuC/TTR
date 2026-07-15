#include "explorer_session.h"
#include <TlHelp32.h>
#include <filesystem>
#include <vector>
namespace ttr::host
{
void CloseExplorerInfo(ExplorerInfo& i) noexcept
{
  if (i.process)
    CloseHandle(i.process);
  i = {};
}
bool FindAndValidateExplorer(ExplorerInfo& i, std::wstring& e) noexcept
{
  CloseExplorerInfo(i);
  i.taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (!i.taskbar)
  {
    e = L"The Explorer taskbar is not available.";
    return false;
  }
  i.threadId = GetWindowThreadProcessId(i.taskbar, &i.processId);
  DWORD a{}, b{};
  if (!i.threadId || !ProcessIdToSessionId(GetCurrentProcessId(), &a) ||
      !ProcessIdToSessionId(i.processId, &b) || a != b)
  {
    e = L"Explorer is in a different session.";
    return false;
  }
  i.process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, i.processId);
  if (!i.process)
  {
    e = L"Unable to query Explorer.";
    return false;
  }
  std::vector<wchar_t> p(32768);
  DWORD n = static_cast<DWORD>(p.size());
  if (!QueryFullProcessImageNameW(i.process, 0, p.data(), &n))
  {
    e = L"Unable to resolve Explorer's path.";
    CloseExplorerInfo(i);
    return false;
  }
  i.path.assign(p.data(), n);
  wchar_t windows[MAX_PATH]{};
  const UINT windowsLength = GetWindowsDirectoryW(windows, MAX_PATH);
  if (!windowsLength || windowsLength >= MAX_PATH)
  {
    e = L"Unable to resolve the Windows directory.";
    CloseExplorerInfo(i);
    return false;
  }
  std::error_code actualError, expectedError;
  auto actual = std::filesystem::weakly_canonical(i.path, actualError),
       expected = std::filesystem::weakly_canonical(
           std::filesystem::path(windows) / L"explorer.exe", expectedError);
  if (actualError || expectedError || _wcsicmp(actual.c_str(), expected.c_str()) != 0)
  {
    e = L"The taskbar owner is not the system Explorer.";
    CloseExplorerInfo(i);
    return false;
  }
  USHORT pm{}, nm{};
  if (!IsWow64Process2(i.process, &pm, &nm) || pm != IMAGE_FILE_MACHINE_UNKNOWN ||
      nm != IMAGE_FILE_MACHINE_AMD64)
  {
    e = L"Explorer is not native x64.";
    CloseExplorerInfo(i);
    return false;
  }
  HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, i.processId);
  if (s == INVALID_HANDLE_VALUE)
  {
    e = L"Unable to enumerate Explorer modules.";
    CloseExplorerInfo(i);
    return false;
  }
  MODULEENTRY32W m{sizeof(m)};
  if (Module32FirstW(s, &m))
    do
    {
      if (_wcsicmp(m.szModule, L"taskbar.dll") == 0 ||
          _wcsicmp(m.szModule, L"Taskbar.View.dll") == 0 ||
          _wcsicmp(m.szModule, L"ExplorerExtensions.dll") == 0)
      {
        PeIdentity id;
        std::string pe;
        if (ReadPeIdentity(m.szExePath, id, pe))
          i.modules.push_back(std::move(id));
      }
    } while (Module32NextW(s, &m));
  CloseHandle(s);
  return true;
}
} // namespace ttr::host
