#include "sha256.h"
#include <bcrypt.h>
#include <vector>

namespace ttr
{
bool Sha256Bytes(std::span<const std::byte> bytes, Sha256& out) noexcept
{
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_HASH_HANDLE hash{};
  DWORD objectBytes{}, resultBytes{};
  std::vector<UCHAR> object;
  bool ok = false;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    goto done;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
                        sizeof(objectBytes), &resultBytes, 0) < 0)
    goto done;
  object.resize(objectBytes);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) < 0)
    goto done;
  if (!bytes.empty() &&
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
                     static_cast<ULONG>(bytes.size()), 0) < 0)
    goto done;
  if (BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(out.size()),
                       0) < 0)
    goto done;
  ok = true;
done:
  if (hash)
    BCryptDestroyHash(hash);
  if (algorithm)
    BCryptCloseAlgorithmProvider(algorithm, 0);
  return ok;
}

bool Sha256File(const std::wstring& path, Sha256& out) noexcept
{
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_HASH_HANDLE hash{};
  DWORD objectBytes{}, resultBytes{};
  std::vector<UCHAR> object;
  bool ok = false;
  std::vector<std::byte> buffer(64 * 1024);
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    goto done;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
                        sizeof(objectBytes), &resultBytes, 0) < 0)
    goto done;
  object.resize(objectBytes);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) < 0)
    goto done;
  for (;;)
  {
    DWORD read{};
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
      goto done;
    if (!read)
      break;
    if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), read, 0) < 0)
      goto done;
  }
  if (BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(out.size()),
                       0) < 0)
    goto done;
  ok = true;
done:
  if (hash)
    BCryptDestroyHash(hash);
  if (algorithm)
    BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  return ok;
}

std::wstring Sha256Hex(const Sha256& hash)
{
  constexpr wchar_t hex[] = L"0123456789abcdef";
  std::wstring result;
  result.reserve(hash.size() * 2);
  for (auto value : hash)
  {
    auto b = std::to_integer<unsigned char>(value);
    result.push_back(hex[b >> 4]);
    result.push_back(hex[b & 15]);
  }
  return result;
}
} // namespace ttr
