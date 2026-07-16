#include "manifest_update.h"

#include "autostart.h"
#include "crypto.h"
#include "ttr_manifest.h"
#include "ttr_version.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
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

bool WriteFileFully(const std::wstring& path, const std::span<const std::byte> bytes)
{
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  std::size_t completed = 0;
  bool valid = true;
  while (completed < bytes.size())
  {
    const DWORD request =
        static_cast<DWORD>(std::min<std::size_t>(bytes.size() - completed, MAXDWORD));
    DWORD written = 0;
    if (!WriteFile(file, bytes.data() + completed, request, &written, nullptr) || !written)
    {
      valid = false;
      break;
    }
    completed += written;
  }
  valid = valid && FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  return valid;
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

bool InstallPair(const std::vector<std::byte>& manifest, const std::vector<std::byte>& signature,
                 const std::uint64_t sequence)
{
  const auto directory = ApplicationDataDirectory() + L"\\compat";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error)
    return false;
  const auto manifestPath = directory + L"\\compat.bin";
  const auto signaturePath = directory + L"\\compat.sig";
  const auto manifestNew = manifestPath + L".new";
  const auto signatureNew = signaturePath + L".new";
  const auto manifestBackup = manifestPath + L".bak";
  const auto signatureBackup = signaturePath + L".bak";
  DeleteFileW(manifestNew.c_str());
  DeleteFileW(signatureNew.c_str());
  if (!WriteFileFully(manifestNew, manifest) || !WriteFileFully(signatureNew, signature))
  {
    DeleteFileW(manifestNew.c_str());
    DeleteFileW(signatureNew.c_str());
    return false;
  }
  if (std::filesystem::exists(manifestPath, error) && std::filesystem::exists(signaturePath, error))
  {
    CopyFileW(manifestPath.c_str(), manifestBackup.c_str(), FALSE);
    CopyFileW(signaturePath.c_str(), signatureBackup.c_str(), FALSE);
  }
  const bool replacedManifest =
      MoveFileExW(manifestNew.c_str(), manifestPath.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  const bool replacedSignature =
      replacedManifest && MoveFileExW(signatureNew.c_str(), signaturePath.c_str(),
                                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  if (!replacedSignature || !SaveHighestInstalledSequence(sequence))
  {
    DeleteFileW(manifestNew.c_str());
    DeleteFileW(signatureNew.c_str());
    if (std::filesystem::exists(manifestBackup, error) &&
        std::filesystem::exists(signatureBackup, error))
    {
      CopyFileW(manifestBackup.c_str(), manifestPath.c_str(), FALSE);
      CopyFileW(signatureBackup.c_str(), signaturePath.c_str(), FALSE);
    }
    else
    {
      DeleteFileW(manifestPath.c_str());
      DeleteFileW(signaturePath.c_str());
    }
    return false;
  }
  return true;
}

} // namespace

void RunCompatibilityUpdate(const std::stop_token stopToken, const HWND owner) noexcept
{
  ManifestUpdateResult result = ManifestUpdateResult::Rejected;
  if (!*TTR_MANIFEST_URL_W)
  {
    result = ManifestUpdateResult::NotConfigured;
  }
  else
  {
    std::vector<std::byte> manifest;
    std::vector<std::byte> signature;
    std::span<const std::byte> publicKey;
    ManifestView view;
    std::string error;
    const std::wstring url = TTR_MANIFEST_URL_W;
    const bool valid =
        Download(url, kMaxManifestBytes, stopToken, manifest) &&
        Download(url + L".sig", 256, stopToken, signature) && EmbeddedPublicKey(publicKey) &&
        VerifyEcdsaP256(publicKey, manifest, signature, error) &&
        ParseManifest(manifest, view, error) && view.header->sequence > HighestInstalledSequence();
    if (stopToken.stop_requested())
    {
      result = ManifestUpdateResult::Cancelled;
    }
    else if (valid && InstallPair(manifest, signature, view.header->sequence))
    {
      result = ManifestUpdateResult::Installed;
    }
  }
  PostMessageW(owner, kUpdateCompleteMessage, static_cast<WPARAM>(result), 0);
}

} // namespace ttr::host
