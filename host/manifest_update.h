#pragma once
#include <Windows.h>

#include <stop_token>
#include <cstdint>

namespace ttr::host
{

enum class ManifestUpdateResult : WPARAM
{
  Installed = 1,
  NotConfigured,
  Cancelled,
  NoNewer,
  InvalidSignature,
  NetworkUnavailable,
  InstallFailed,
};

void RunCompatibilityUpdate(std::stop_token stopToken, HWND owner,
                            std::uint64_t minimumSequence, bool manual) noexcept;

} // namespace ttr::host
