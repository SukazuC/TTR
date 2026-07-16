#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace ttr
{

inline constexpr std::size_t kEcdsaP256SignatureBytes = 64;

bool ValidateEcdsaP256PublicKey(std::span<const std::byte> publicKey, std::string& error) noexcept;
bool VerifyEcdsaP256(std::span<const std::byte> publicKey, std::span<const std::byte> data,
                     std::span<const std::byte> signature, std::string& error) noexcept;

} // namespace ttr
