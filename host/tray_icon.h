#pragma once
#include <Windows.h>
#include <shellapi.h>
namespace ttr::host { enum class TrayState{Active,Disabled,Warning,Faulted};class TrayIcon{public:bool Add(HWND,UINT,TrayState)noexcept;void Remove()noexcept;void SetState(TrayState)noexcept;private:NOTIFYICONDATAW data_{};TrayState state_{TrayState::Disabled};}; }
