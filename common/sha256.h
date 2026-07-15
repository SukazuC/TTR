#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace ttr {
using Sha256 = std::array<std::byte, 32>;
bool Sha256Bytes(std::span<const std::byte> bytes, Sha256& out) noexcept;
bool Sha256File(const std::wstring& path, Sha256& out) noexcept;
std::wstring Sha256Hex(const Sha256& hash);
}
