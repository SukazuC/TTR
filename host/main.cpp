#include "app.h"
#include "cleanup.h"
#include <Windows.h>
#include <shellapi.h>
#include <string>

int WINAPI wWinMain(_In_ HINSTANCE instance,_In_opt_ HINSTANCE,_In_ PWSTR,_In_ int show){
  int argc{};LPWSTR*argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool replace=false;
  if(argv){for(int i=1;i<argc;++i)if(_wcsicmp(argv[i],L"--replace-current")==0)replace=true;if(argc==4&&_wcsicmp(argv[1],L"--cleanup-after")==0){wchar_t*end{};auto pid=wcstoul(argv[2],&end,10);bool valid=end&&!*end;std::wstring directory=argv[3];LocalFree(argv);return valid?ttr::host::RunCleanupHelper(pid,directory):2;}LocalFree(argv);}
  HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\TaskbarThumbnailReorder.Host.v1");if(!mutex)return 1;
  if(GetLastError()==ERROR_ALREADY_EXISTS){auto window=FindWindowW(ttr::host::App::kWindowClass,nullptr);if(replace){if(window)SendMessageTimeoutW(window,RegisterWindowMessageW(L"TaskbarThumbnailReorder.Replace.v1"),0,0,SMTO_ABORTIFHUNG,5000,nullptr);if(WaitForSingleObject(mutex,10000)!=WAIT_OBJECT_0){CloseHandle(mutex);return 1;}}else{if(window)PostMessageW(window,RegisterWindowMessageW(L"TaskbarThumbnailReorder.Activate.v1"),0,0);CloseHandle(mutex);return 0;}}
  ttr::host::App app;int result=app.Run(instance,show);ReleaseMutex(mutex);CloseHandle(mutex);return result;
}
