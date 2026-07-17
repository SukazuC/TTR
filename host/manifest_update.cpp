#include "manifest_update.h"

#include "autostart.h"
#include "crypto.h"
#include "manifest_install.h"
#include "ttr_manifest.h"
#include "ttr_version.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace ttr::host
{
namespace
{

constexpr UINT kUpdateCompleteMessage = WM_APP + 3;
constexpr wchar_t kSettingsKey[] = L"Software\\TaskbarThumbnailReorder";

class InternetHandle
{
public:
  InternetHandle() = default;
  explicit InternetHandle(HINTERNET handle) : handle_(handle) {}
  ~InternetHandle()
  {
    if (handle_)
      WinHttpCloseHandle(handle_);
  }
  InternetHandle(const InternetHandle&) = delete;
  InternetHandle& operator=(const InternetHandle&) = delete;
  [[nodiscard]] HINTERNET get() const noexcept
  {
    return handle_;
  }
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return handle_ != nullptr;
  }

private:
  HINTERNET handle_{};
};

bool Download(const std::wstring& url, const std::size_t limit, const std::stop_token stopToken,
              std::vector<std::byte>& output)
{
  URL_COMPONENTS parts{sizeof(parts)};
  wchar_t host[256]{};
  wchar_t path[2048]{};
  parts.lpszHostName = host;
  parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
  {
    return false;
  }
  InternetHandle session(WinHttpOpen(L"TaskbarThumbnailReorder/" TTR_VERSION_W,
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session)
    return false;
  WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 5000);
  InternetHandle connection(WinHttpConnect(session.get(), host, parts.nPort, 0));
  if (!connection)
    return false;
  InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path, nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE));
  if (!request || stopToken.stop_requested() ||
      !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                          0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr))
  {
    return false;
  }
  DWORD status = 0;
  DWORD statusBytes = sizeof(status);
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes,
                           WINHTTP_NO_HEADER_INDEX) ||
      status != HTTP_STATUS_OK)
  {
    return false;
  }
  while (!stopToken.stop_requested())
  {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.get(), &available))
      return false;
    if (!available)
      return !output.empty();
    if (available > limit - output.size())
      return false;
    const auto previous = output.size();
    output.resize(previous + available);
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), output.data() + previous, available, &read) || !read)
    {
      return false;
    }
    output.resize(previous + read);
  }
  return false;
}

bool EmbeddedPublicKey(std::span<const std::byte>& key)
{
  const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(102), RT_RCDATA);
  const auto loaded = resource ? LoadResource(nullptr, resource) : nullptr;
  const auto* data = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
  const DWORD size = resource ? SizeofResource(nullptr, resource) : 0;
  if (!data || !size)
    return false;
  key = {data, size};
  return true;
}

std::uint64_t HighestInstalledSequence()
{
  HKEY key{};
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
  {
    return 0;
  }
  ULONGLONG value = 0;
  DWORD size = sizeof(value);
  const auto status = RegQueryValueExW(key, L"ManifestSequence", nullptr, nullptr,
                                       reinterpret_cast<BYTE*>(&value), &size);
  RegCloseKey(key);
  return status == ERROR_SUCCESS && size == sizeof(value) ? value : 0;
}

bool SaveHighestInstalledSequence(const std::uint64_t sequence)
{
  HKEY key{};
  DWORD disposition = 0;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      &disposition) != ERROR_SUCCESS)
  {
    return false;
  }
  const auto status = RegSetValueExW(key, L"ManifestSequence", 0, REG_QWORD,
                                     reinterpret_cast<const BYTE*>(&sequence), sizeof(sequence));
  RegCloseKey(key);
  return status == ERROR_SUCCESS;
}

bool SaveSequence(const std::uint64_t sequence, void*)
{
  return SaveHighestInstalledSequence(sequence);
}

std::wstring SignatureUrl(std::wstring url)
{
  const auto query = url.find_first_of(L"?#");
  const auto end = query == std::wstring::npos ? url.size() : query;
  const auto slash = url.rfind(L'/', end);
  const auto dot = url.rfind(L'.', end);
  if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
    url.replace(dot, end - dot, L".sig");
  else
    url.insert(end, L".sig");
  return url;
}

} // namespace

void RunCompatibilityUpdate(const std::stop_token stopToken, const HWND owner,
                            const std::uint64_t minimumSequence, const bool manual) noexcept
{
  ManifestUpdateResult result = ManifestUpdateResult::InstallFailed;
  if (!*TTR_MANIFEST_URL_W)
  {
    result = ManifestUpdateResult::NotConfigured;
  }
  else
  {
    std::vector<std::byte> manifest;
    std::vector<std::byte> signature;
    std::span<const std::byte> publicKey;
    std::string error;
    const std::wstring url = TTR_MANIFEST_URL_W;
    if (!Download(url, kMaxManifestBytes, stopToken, manifest) ||
        !Download(SignatureUrl(url), 256, stopToken, signature))
      result = stopToken.stop_requested() ? ManifestUpdateResult::Cancelled
                                          : ManifestUpdateResult::NetworkUnavailable;
    else if (!EmbeddedPublicKey(publicKey))
      result = ManifestUpdateResult::InstallFailed;
    else
    {
      std::uint64_t installedSequence = 0;
      const auto floor = std::max(minimumSequence, HighestInstalledSequence());
      const auto installed = InstallSignedManifestPair(
          publicKey, manifest, signature, ApplicationDataDirectory() + L"\\compat", floor,
          SaveSequence, nullptr, installedSequence, error);
      switch (installed)
      {
      case ManifestInstallResult::Installed:
        result = ManifestUpdateResult::Installed;
        break;
      case ManifestInstallResult::NotNewer:
        result = ManifestUpdateResult::NoNewer;
        break;
      case ManifestInstallResult::InvalidSignature:
      case ManifestInstallResult::InvalidManifest:
        result = ManifestUpdateResult::InvalidSignature;
        break;
      default:
        result = ManifestUpdateResult::InstallFailed;
        break;
      }
    }
  }
  PostMessageW(owner, kUpdateCompleteMessage, static_cast<WPARAM>(result), manual ? 1 : 0);
}

} // namespace ttr::host
