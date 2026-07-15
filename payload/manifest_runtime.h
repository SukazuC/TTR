#pragma once
#include "ttr_manifest.h"
#include <Windows.h>
#include <array>
#include <span>

namespace ttr::payload
{
struct Compatibility
{
  std::uint64_t recordId{};
  std::uint32_t backendFlags{};
  std::array<void*, static_cast<size_t>(SymbolId::Last) + 1> symbols{};
  bool hasTaskListUiAdjustment{};
  std::uint32_t taskListUiOffset{};
  std::uint32_t taskListObjectSize{};
};
bool ResolveCompatibility(std::span<const std::byte>, Compatibility&) noexcept;
} // namespace ttr::payload
