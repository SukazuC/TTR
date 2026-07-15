#include "settings.h"
namespace ttr::host
{
namespace
{
constexpr wchar_t kKey[] = L"Software\\TaskbarThumbnailReorder";
}
Settings LoadSettings() noexcept
{
  Settings r;
  HKEY k{};
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
    return r;
  DWORD v{}, n = sizeof(v);
  if (RegQueryValueExW(k, L"Enabled", nullptr, nullptr, reinterpret_cast<BYTE*>(&v), &n) ==
      ERROR_SUCCESS)
    r.enabled = v != 0;
  n = sizeof(v);
  if (RegQueryValueExW(k, L"CheckManifestUpdates", nullptr, nullptr, reinterpret_cast<BYTE*>(&v),
                       &n) == ERROR_SUCCESS)
    r.checkManifestUpdates = v != 0;
  ULONGLONG q{};
  n = sizeof(q);
  if (RegQueryValueExW(k, L"LastManifestCheckUtc", nullptr, nullptr, reinterpret_cast<BYTE*>(&q),
                       &n) == ERROR_SUCCESS)
    r.lastManifestCheckUtc = q;
  RegCloseKey(k);
  return r;
}
bool SaveEnabled(bool e) noexcept
{
  HKEY k{};
  DWORD d{};
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, &d) !=
      ERROR_SUCCESS)
    return false;
  DWORD v = e;
  auto s =
      RegSetValueExW(k, L"Enabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
  RegCloseKey(k);
  return s == ERROR_SUCCESS;
}
} // namespace ttr::host
