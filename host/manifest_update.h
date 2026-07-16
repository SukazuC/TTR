#pragma once

#include <Windows.h>

#include <stop_token>

namespace ttr::host
{

enum class ManifestUpdateResult : WPARAM
{
  Installed = 1,
  NotConfigured,
  Cancelled,
  Rejected,
};

void RunCompatibilityUpdate(std::stop_token stopToken, HWND owner) noexcept;

} // namespace ttr::host
