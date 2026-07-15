#pragma once
#include <Windows.h>
#include <string>
namespace ttr::host
{
bool LaunchCleanupHelper(DWORD, const std::wstring&, std::wstring&) noexcept;
int RunCleanupHelper(DWORD, const std::wstring&) noexcept;
} // namespace ttr::host
