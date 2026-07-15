#include "crypto.h"

#include "sha256.h"

#include <Windows.h>
#include <bcrypt.h>

#include <limits>

namespace ttr
{
namespace
{

bool StructurallyValidKey(const std::span<const std::byte> key, std::string& error) noexcept
{
  if (key.size() < sizeof(BCRYPT_ECCKEY_BLOB))
  {
    error = "public key blob is truncated";
    return false;
  }
  const auto* header = reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(key.data());
  if (header->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC || header->cbKey != 32)
  {
    error = "public key is not an ECDSA P-256 public blob";
    return false;
  }
  constexpr std::size_t coordinateCount = 2;
  if (header->cbKey >
          (std::numeric_limits<std::size_t>::max() - sizeof(*header)) / coordinateCount ||
      key.size() != sizeof(*header) + coordinateCount * header->cbKey)
  {
    error = "public key blob has an invalid length";
    return false;
  }
  return true;
}

bool ImportPublicKey(const std::span<const std::byte> key, BCRYPT_ALG_HANDLE& algorithm,
                     BCRYPT_KEY_HANDLE& imported, std::string& error) noexcept
{
  if (!StructurallyValidKey(key, error))
  {
    return false;
  }
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0)
  {
    error = "unable to open the ECDSA P-256 provider";
    return false;
  }
  if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &imported,
                          reinterpret_cast<PUCHAR>(const_cast<std::byte*>(key.data())),
                          static_cast<ULONG>(key.size()), 0) < 0)
  {
    error = "public key coordinates are invalid";
    return false;
  }
  return true;
}

} // namespace

bool ValidateEcdsaP256PublicKey(const std::span<const std::byte> publicKey,
                                std::string& error) noexcept
{
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_KEY_HANDLE key{};
  const bool valid = ImportPublicKey(publicKey, algorithm, key, error);
  if (key)
  {
    BCryptDestroyKey(key);
  }
  if (algorithm)
  {
    BCryptCloseAlgorithmProvider(algorithm, 0);
  }
  return valid;
}

bool VerifyEcdsaP256(const std::span<const std::byte> publicKey,
                     const std::span<const std::byte> data,
                     const std::span<const std::byte> signature, std::string& error) noexcept
{
  if (signature.size() != kEcdsaP256SignatureBytes)
  {
    error = "ECDSA P-256 signature must be exactly 64 bytes";
    return false;
  }
  Sha256 digest{};
  if (!Sha256Bytes(data, digest))
  {
    error = "unable to hash signed data";
    return false;
  }
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_KEY_HANDLE key{};
  bool valid = ImportPublicKey(publicKey, algorithm, key, error);
  if (valid &&
      BCryptVerifySignature(key, nullptr, reinterpret_cast<PUCHAR>(digest.data()),
                            static_cast<ULONG>(digest.size()),
                            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(signature.data())),
                            static_cast<ULONG>(signature.size()), 0) < 0)
  {
    error = "signature verification failed";
    valid = false;
  }
  if (key)
  {
    BCryptDestroyKey(key);
  }
  if (algorithm)
  {
    BCryptCloseAlgorithmProvider(algorithm, 0);
  }
  return valid;
}

} // namespace ttr
