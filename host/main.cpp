#include "app.h"
#include "cleanup.h"
#include "offline_diagnostics.h"
#include "ttr_version.h"
#include <Windows.h>
#include <shellapi.h>
#include <string>

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int show)
{
  int argc{};
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool replace = false;
  if (argv)
  {
    if (argc >= 2 && _wcsicmp(argv[1], L"--diagnose-offline") == 0)
    {
      const int result = ttr::host::RunOfflineDiagnostics(argc, argv);
      LocalFree(argv);
      return result;
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--version") == 0)
    {
      const std::string text = std::string("Taskbar Thumbnail Reorder ") + TTR_VERSION_A + "\r\n";
      DWORD written{};
      const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
      if (output && output != INVALID_HANDLE_VALUE)
        WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
      LocalFree(argv);
      return 0;
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--host-startup-smoke") == 0)
    {
      LocalFree(argv);
      return ttr::host::App::RunStartupSmoke(instance);
    }
    for (int i = 1; i < argc; ++i)
      if (_wcsicmp(argv[i], L"--replace-current") == 0)
        replace = true;
    if (argc == 4 && _wcsicmp(argv[1], L"--cleanup-after") == 0)
    {
      wchar_t* end{};
      auto pid = wcstoul(argv[2], &end, 10);
      bool valid = end && !*end;
      std::wstring directory = argv[3];
      LocalFree(argv);
      return valid ? ttr::host::RunCleanupHelper(pid, directory) : 2;
    }
    LocalFree(argv);
  }
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\TaskbarThumbnailReorder.Host.v1");
  if (!mutex)
    return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS)
  {
    auto window = FindWindowW(ttr::host::App::kWindowClass, nullptr);
    if (replace)
    {
      if (window)
        SendMessageTimeoutW(window, RegisterWindowMessageW(L"TaskbarThumbnailReorder.Replace.v1"),
                            0, 0, SMTO_ABORTIFHUNG, 5000, nullptr);
      if (WaitForSingleObject(mutex, 10000) != WAIT_OBJECT_0)
      {
        CloseHandle(mutex);
        return 1;
      }
    }
    else
    {
      if (window)
        PostMessageW(window, RegisterWindowMessageW(L"TaskbarThumbnailReorder.Activate.v1"), 0, 0);
      CloseHandle(mutex);
      return 0;
    }
  }
  ttr::host::App app;
  int result = app.Run(instance, show);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return result;
}
