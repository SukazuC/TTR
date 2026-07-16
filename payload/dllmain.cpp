#include "payload_state.h"
#include <Windows.h>
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
  if (reason == DLL_PROCESS_ATTACH)
  {
    ttr::payload::GetState().instance = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}
