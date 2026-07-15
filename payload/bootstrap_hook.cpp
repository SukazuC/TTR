#include "bootstrap_hook.h"
#include "payload_state.h"
#include "seh_filter.h"
extern "C" LRESULT CALLBACK TtrCallWndProcHook(int code,WPARAM w,LPARAM l)noexcept{LRESULT next=CallNextHookEx(nullptr,code,w,l);if(code>=0){__try{ttr::payload::ProcessCommand();}__except(ttr::payload::SehFilter(GetExceptionCode())){ttr::payload::MarkFault(ttr::PayloadError::StructuredException);}}return next;}
