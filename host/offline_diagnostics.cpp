#include "offline_diagnostics.h"

#include "crypto.h"
#include "pe_identity.h"
#include "sha256.h"
#include "ttr_manifest.h"
#include "ttr_version.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ttr::host
{
namespace
{

constexpr int kPayloadResource = 101;
constexpr int kPublicKeyResource = 102;

bool ReadBounded(const std::wstring& path, const std::size_t limit, std::vector<std::byte>& bytes,
                 std::string& error)
{
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file)
  {
    error = "unable to open file";
    return false;
  }
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  file.seekg(0);
  if (size < 0 || size > static_cast<std::streamoff>(limit))
  {
    error = "file size is outside the supported limit";
    return false;
  }
  bytes.resize(static_cast<std::size_t>(size));
  if (!bytes.empty())
  {
    file.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!file)
  {
    error = "unable to read complete file";
    return false;
  }
  return true;
}

bool ResourceBytes(const int identifier, std::span<const std::byte>& bytes,
                   std::string& error) noexcept
{
  const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(identifier), RT_RCDATA);
  if (!resource)
  {
    error = "resource is missing";
    return false;
  }
  const auto loaded = LoadResource(nullptr, resource);
  const auto size = SizeofResource(nullptr, resource);
  const auto* data = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
  if (!loaded || !data || !size)
  {
    error = "resource is empty or unreadable";
    return false;
  }
  bytes = {data, size};
  return true;
}

std::wstring Wide(const std::string_view value)
{
  if (value.empty())
    return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0)
    return L"<invalid UTF-8>";
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                      result.data(), count);
  return result;
}

std::string Utf8(const std::wstring_view value)
{
  if (value.empty())
    return {};
  const int count =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (count <= 0)
    return {};
  std::string result(static_cast<std::size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                      result.data(), count, nullptr, nullptr);
  return result;
}

std::wstring EscapeJson(const std::wstring_view value)
{
  std::wstring result;
  for (const auto character : value)
  {
    if (character == L'\\' || character == L'"')
      result.push_back(L'\\');
    if (character == L'\n')
    {
      result += L"\\n";
    }
    else
    {
      result.push_back(character);
    }
  }
  return result;
}

void WriteOutput(const std::wstring& text) noexcept
{
  const auto utf8 = Utf8(text);
  const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (output && output != INVALID_HANDLE_VALUE && !utf8.empty())
  {
    DWORD written = 0;
    WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
  }
}

struct ModuleCheck
{
  std::wstring path;
  PeImage image;
  PeIdentity identity;
  std::string error;
  bool genericValid{};
  bool exactValid{};
};

std::vector<ModuleCheck> SystemModules()
{
  wchar_t windows[MAX_PATH]{};
  const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
  if (!length || length >= MAX_PATH)
    return {};
  std::vector<ModuleCheck> modules;
  modules.push_back({(std::filesystem::path(windows) / L"explorer.exe").wstring()});
  const auto taskbar = std::filesystem::path(windows) / L"System32" / L"taskbar.dll";
  if (std::filesystem::exists(taskbar))
    modules.push_back({taskbar.wstring()});
  for (auto& module : modules)
  {
    module.genericValid = InspectPeImage(module.path, module.image, module.error);
    if (module.genericValid)
    {
      module.exactValid = RequireCodeViewIdentity(module.image, module.identity, module.error);
    }
  }
  return modules;
}

int ParseArguments(const int argc, wchar_t** argv, bool& json, std::wstring& manifest,
                   std::wstring& signature)
{
  for (int index = 2; index < argc; ++index)
  {
    if (_wcsicmp(argv[index], L"--json") == 0)
    {
      json = true;
    }
    else if (_wcsicmp(argv[index], L"--manifest") == 0 && index + 1 < argc)
    {
      manifest = argv[++index];
    }
    else if (_wcsicmp(argv[index], L"--signature") == 0 && index + 1 < argc)
    {
      signature = argv[++index];
    }
    else
    {
      return 2;
    }
  }
  return manifest.empty() == signature.empty() ? 0 : 2;
}

} // namespace

int RunOfflineDiagnostics(const int argc, wchar_t** argv) noexcept
{
  bool json = false;
  std::wstring manifestPath;
  std::wstring signaturePath;
  const int argumentResult = ParseArguments(argc, argv, json, manifestPath, signaturePath);
  if (argumentResult)
  {
    WriteOutput(L"usage: TaskbarThumbnailReorder.exe --diagnose-offline "
                L"[--json] [--manifest <compat.bin> --signature <compat.sig>]\n");
    return argumentResult;
  }

  std::vector<std::wstring> reasons;
  std::span<const std::byte> payloadBytes;
  std::span<const std::byte> publicKey;
  std::string error;
  PeImage payload;
  Sha256 payloadHash{};
  const bool payloadResource = ResourceBytes(kPayloadResource, payloadBytes, error);
  const bool payloadValid = payloadResource &&
                            InspectPeImageBytes(payloadBytes, "ttrhook64.dll", payload, error) &&
                            Sha256Bytes(payloadBytes, payloadHash);
  if (!payloadValid)
    reasons.push_back(L"embedded payload: " + Wide(error));

  error.clear();
  const bool keyResource = ResourceBytes(kPublicKeyResource, publicKey, error);
  const bool keyValid = keyResource && ValidateEcdsaP256PublicKey(publicKey, error);
  if (!keyValid)
  {
    reasons.push_back(L"public key: fail-closed development key (" + Wide(error) + L")");
  }

  auto modules = SystemModules();
  const bool modulesValid =
      !modules.empty() && std::all_of(modules.begin(), modules.end(),
                                      [](const ModuleCheck& module) { return module.exactValid; });
  for (const auto& module : modules)
  {
    if (!module.exactValid)
    {
      reasons.push_back(L"system module " + module.path + L": " + Wide(module.error));
    }
  }

  bool manifestRequested = !manifestPath.empty();
  bool manifestValid = false;
  bool recordMatched = false;
  std::uint64_t recordId = 0;
  if (manifestRequested)
  {
    std::vector<std::byte> manifest;
    std::vector<std::byte> signature;
    ManifestView view;
    error.clear();
    manifestValid = ReadBounded(manifestPath, kMaxManifestBytes, manifest, error) &&
                    ReadBounded(signaturePath, 256, signature, error) && keyValid &&
                    VerifyEcdsaP256(publicKey, manifest, signature, error) &&
                    ParseManifest(manifest, view, error);
    if (manifestValid)
    {
      std::vector<ModuleIdentityV1> identities;
      for (const auto& module : modules)
      {
        if (module.exactValid)
          identities.push_back(module.identity.identity);
      }
      bool ambiguous = false;
      const auto* record = SelectExactRecord(view, identities, ambiguous);
      if (ambiguous)
      {
        error = "more than one compatibility record matches";
      }
      else if (!record)
      {
        error = "no exact compatibility record matches installed modules";
      }
      else
      {
        recordMatched = true;
        recordId = record->recordId;
        const auto expected = RecordModules(view, *record);
        for (const auto& symbol : RecordSymbols(view, *record))
        {
          const auto found =
              std::find_if(modules.begin(), modules.end(), [&](const ModuleCheck& module) {
                return module.exactValid &&
                       ModuleIdentityEqual(expected[symbol.moduleIndex], module.identity.identity);
              });
          if (found == modules.end() ||
              !ValidateSymbolRva(found->image, symbol.rva, static_cast<SymbolKind>(symbol.kind)))
          {
            recordMatched = false;
            error = "record contains an RVA with invalid range or section permissions";
            break;
          }
        }
      }
    }
    if (!manifestValid || !recordMatched)
    {
      reasons.push_back(L"manifest: " + Wide(error));
    }
  }

  const bool success = payloadValid && modulesValid && keyValid &&
                       (!manifestRequested || (manifestValid && recordMatched));
  std::wostringstream report;
  if (json)
  {
    report << L"{\"schema\":1,\"version\":\"" << TTR_VERSION_W
           << L"\",\"mode\":\"offline\",\"live_integration\":false" << L",\"payload_valid\":"
           << (payloadValid ? L"true" : L"false") << L",\"payload_sha256\":\""
           << (payloadValid ? Sha256Hex(payloadHash) : L"") << L"\",\"public_key_valid\":"
           << (keyValid ? L"true" : L"false") << L",\"modules\":[";
    for (std::size_t index = 0; index < modules.size(); ++index)
    {
      if (index)
        report << L',';
      const auto& module = modules[index];
      report << L"{\"path\":\"" << EscapeJson(module.path) << L"\",\"pe_valid\":"
             << (module.genericValid ? L"true" : L"false") << L",\"codeview_valid\":"
             << (module.exactValid ? L"true" : L"false");
      if (module.exactValid)
      {
        report << L",\"pdb_guid\":\"" << GuidString(module.identity.identity.pdbGuid)
               << L"\",\"pdb_age\":" << module.identity.identity.pdbAge;
      }
      report << L'}';
    }
    report << L"],\"manifest_requested\":" << (manifestRequested ? L"true" : L"false")
           << L",\"manifest_valid\":" << (manifestValid ? L"true" : L"false")
           << L",\"record_matched\":" << (recordMatched ? L"true" : L"false") << L",\"record_id\":"
           << recordId << L",\"reasons\":[";
    for (std::size_t index = 0; index < reasons.size(); ++index)
    {
      if (index)
        report << L',';
      report << L'"' << EscapeJson(reasons[index]) << L'"';
    }
    report << L"],\"result\":\"" << (success ? L"pass" : L"fail") << L"\"}\n";
  }
  else
  {
    report << L"Taskbar Thumbnail Reorder offline diagnostic\n"
           << L"Version: " << TTR_VERSION_W << L"\n"
           << L"Live Explorer integration: NOT EXECUTED\n"
           << L"Embedded payload: " << (payloadValid ? L"valid x64 PE32+" : L"invalid") << L"\n";
    if (payloadValid)
      report << L"Payload SHA-256: " << Sha256Hex(payloadHash) << L"\n";
    report << L"Compatibility public key: " << (keyValid ? L"valid" : L"invalid (fail closed)")
           << L"\n";
    for (const auto& module : modules)
    {
      report << L"Module: " << module.path << L"\n  PE: "
             << (module.genericValid ? L"valid x64 PE32+" : L"invalid") << L"\n  CodeView: ";
      if (module.exactValid)
      {
        report << GuidString(module.identity.identity.pdbGuid) << L" age "
               << module.identity.identity.pdbAge;
      }
      else
      {
        report << L"invalid: " << Wide(module.error);
      }
      report << L"\n";
    }
    if (manifestRequested)
    {
      report << L"Supplied manifest: " << (manifestValid ? L"signature valid" : L"invalid")
             << L"\nExact record match: " << (recordMatched ? L"yes" : L"no") << L"\n";
    }
    for (const auto& reason : reasons)
      report << L"REJECT: " << reason << L"\n";
    report << L"Result: " << (success ? L"PASS" : L"FAIL CLOSED") << L"\n";
  }
  WriteOutput(report.str());
  if (success)
    return 0;
  if (!payloadValid)
    return 3;
  if (!keyValid)
    return 4;
  if (!modulesValid)
    return 5;
  return 6;
}

} // namespace ttr::host
