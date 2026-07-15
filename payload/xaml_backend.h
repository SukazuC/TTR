#pragma once
#include "manifest_runtime.h"
namespace ttr::payload
{
bool EnableXaml(const Compatibility&) noexcept;
void DisableXaml() noexcept;
} // namespace ttr::payload
