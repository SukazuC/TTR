#pragma once
#include "ttr_manifest.h"
#include <Windows.h>
#include <span>
#include <string>
#include <vector>

namespace ttr {
struct PeSection { std::uint32_t virtualAddress; std::uint32_t virtualSize; std::uint32_t characteristics; };
struct PeIdentity { ModuleIdentityV1 identity{}; std::vector<PeSection> sections; };
bool ReadPeIdentity(const std::wstring& path, PeIdentity& out, std::string& error) noexcept;
bool ValidateSymbolRva(const PeIdentity&, std::uint32_t rva, SymbolKind kind) noexcept;
std::wstring GuidString(const GuidBytes& guid);
}
