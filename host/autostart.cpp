#include "autostart.h"
#include <ShlObj.h>
#include <Windows.h>
#include <filesystem>
#include <vector>

namespace ttr::host
{
namespace
{
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"TaskbarThumbnailReorder";
std::wstring CurrentExecutable()
{
  std::vector<wchar_t> b(32768);
  DWORD n = GetModuleFileNameW(nullptr, b.data(), static_cast<DWORD>(b.size()));
  return {b.data(), n};
}
bool LaunchReplacement(const std::wstring& target)
{
  auto command = L"\"" + target + L"\" --replace-current";
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  if (!CreateProcessW(target.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      nullptr, &startup, &process))
    return false;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}
} // namespace
std::wstring ApplicationDataDirectory()
{
  PWSTR p{};
  std::wstring r;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &p)))
  {
    r = p;
    CoTaskMemFree(p);
    r += L"\\TaskbarThumbnailReorder";
  }
  return r;
}
bool IsAutostartEnabled() noexcept
{
  HKEY k{};
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
    return false;
  auto s = RegQueryValueExW(k, kRunValue, nullptr, nullptr, nullptr, nullptr);
  RegCloseKey(k);
  return s == ERROR_SUCCESS;
}
bool SetAutostartEnabled(bool enabled, std::wstring& error) noexcept
{
  HKEY key{};
  DWORD disposition{};
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      &disposition) != ERROR_SUCCESS)
  {
    error = L"Unable to open the per-user Run key.";
    return false;
  }
  if (!enabled)
  {
    auto status = RegDeleteValueW(key, kRunValue);
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
  }
  auto directory = ApplicationDataDirectory();
  if (directory.empty())
  {
    RegCloseKey(key);
    error = L"Local AppData is unavailable.";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec)
  {
    RegCloseKey(key);
    error = L"Unable to create the application directory.";
    return false;
  }
  auto target = directory + L"\\TaskbarThumbnailReorder.exe", temporary = target + L".new";
  if (!CopyFileW(CurrentExecutable().c_str(), temporary.c_str(), FALSE) ||
      !MoveFileExW(temporary.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
  {
    DeleteFileW(temporary.c_str());
    RegCloseKey(key);
    error = L"Unable to install the autostart copy.";
    return false;
  }
  auto command = L"\"" + target + L"\" --background";
  auto status =
      RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                     static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  if (status != ERROR_SUCCESS)
  {
    error = L"Unable to write the Run value.";
    return false;
  }
  if (_wcsicmp(CurrentExecutable().c_str(), target.c_str()) != 0 && !LaunchReplacement(target))
  {
    error = L"Autostart was enabled, but the installed copy could not take over until the next "
            L"sign-in.";
    return false;
  }
  return true;
}
} // namespace ttr::host
