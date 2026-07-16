#pragma once
#include <string>
namespace ttr::host
{
bool IsAutostartEnabled() noexcept;
bool SetAutostartEnabled(bool, std::wstring&) noexcept;
std::wstring ApplicationDataDirectory();
} // namespace ttr::host
