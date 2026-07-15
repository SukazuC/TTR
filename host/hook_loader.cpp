#include "hook_loader.h"
#include "autostart.h"
#include "sha256.h"
#include "ttr_manifest.h"
#include "ttr_version.h"
#include <bcrypt.h>
#include <filesystem>
#include <sddl.h>

namespace ttr::host
{
namespace
{
using HookProc = LRESULT(CALLBACK*)(int, WPARAM, LPARAM);
constexpr int kPayloadResource = 101;
bool WriteAll(const std::wstring& path, std::span<const std::byte> bytes)
{
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  DWORD written{};
  bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
            written == bytes.size() && FlushFileBuffers(file);
  CloseHandle(file);
  return ok;
}
bool ProcessIsAlive(DWORD pid)
{
  HANDLE process = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
  if (!process)
    return false;
  bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
  CloseHandle(process);
  return alive;
}
} // namespace

HookLoader::~HookLoader()
{
  Detach();
}

bool HookLoader::ExtractPayload(std::wstring& path, std::wstring& error) noexcept
{
  auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(kPayloadResource), RT_RCDATA);
  if (!resource)
  {
    error = L"The embedded hook payload is missing.";
    return false;
  }
  auto loaded = LoadResource(nullptr, resource);
  if (!loaded)
  {
    error = L"The embedded hook payload is invalid.";
    return false;
  }
  auto* data = static_cast<const std::byte*>(LockResource(loaded));
  DWORD size = SizeofResource(nullptr, resource);
  if (!data || !size)
  {
    error = L"The embedded hook payload is invalid.";
    return false;
  }
  PeImage payloadImage;
  std::string peError;
  if (!InspectPeImageBytes({data, size}, "ttrhook64.dll", payloadImage, peError))
  {
    error = L"The embedded hook payload is not a valid x64 PE image.";
    return false;
  }
  auto directory = ApplicationDataDirectory() + L"\\payload\\" + TTR_VERSION_W;
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec)
  {
    error = L"Unable to create the payload directory.";
    return false;
  }
  path = directory + L"\\TTRHook64.dll";
  auto temporary = path + L".tmp";
  Sha256 expected{}, actual{};
  if (!Sha256Bytes({data, size}, expected))
  {
    error = L"Unable to hash the embedded payload.";
    return false;
  }
  if (std::filesystem::exists(path, ec) && Sha256File(path, actual) && actual == expected)
    return true;
  if (!WriteAll(temporary, {data, size}) || !Sha256File(temporary, actual) || actual != expected ||
      !MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
  {
    DeleteFileW(temporary.c_str());
    error = L"Unable to extract and verify the hook payload.";
    return false;
  }
  return true;
}

bool HookLoader::Attach(const ExplorerInfo& info, std::span<const std::byte> manifest,
                        std::wstring& error) noexcept
{
  if (!Detach())
  {
    error = L"The previous Explorer payload is still draining callbacks.";
    return false;
  }
  if (manifest.empty() || manifest.size() > kMaxManifestBytes)
  {
    error = L"No exact compatibility record is available.";
    return false;
  }
  if (!ExtractPayload(payloadPath_, error))
    return false;
  wchar_t mappingName[96]{};
  swprintf_s(mappingName, L"Local\\TaskbarThumbnailReorder.Session.%lu", info.processId);
  PSECURITY_DESCRIPTOR descriptor{};
  SECURITY_ATTRIBUTES attributes{sizeof(attributes)};
  if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;OW)",
                                                           SDDL_REVISION_1, &descriptor, nullptr))
    attributes.lpSecurityDescriptor = descriptor;
  mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, descriptor ? &attributes : nullptr,
                                PAGE_READWRITE, 0, kSessionMappingBytes, mappingName);
  if (descriptor)
    LocalFree(descriptor);
  if (!mapping_)
  {
    error = L"Unable to create the Explorer session mapping.";
    return false;
  }
  mappingView_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kSessionMappingBytes);
  if (!mappingView_)
  {
    error = L"Unable to map the Explorer session.";
    Detach();
    return false;
  }
  ZeroMemory(mappingView_, kSessionMappingBytes);
  control_ = static_cast<TtrSessionControlV1*>(mappingView_);
  memcpy(control_->magic, kSessionMagic, 8);
  control_->byteSize = sizeof(*control_);
  control_->protocolVersion = kProtocolVersion;
  control_->hostPid = GetCurrentProcessId();
  control_->explorerPid = info.processId;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&control_->sessionNonce),
                      static_cast<ULONG>(sizeof(control_->sessionNonce)),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
  {
    error = L"Unable to generate the session nonce.";
    Detach();
    return false;
  }
  control_->manifestOffset = sizeof(*control_);
  control_->manifestSize = static_cast<std::uint32_t>(manifest.size());
  control_->logOffset = (control_->manifestOffset + control_->manifestSize + 63) & ~63u;
  control_->logSize = kLogRingBytes;
  if (control_->logOffset > kSessionMappingBytes ||
      control_->logSize > kSessionMappingBytes - control_->logOffset)
  {
    error = L"Compatibility data does not fit the session mapping.";
    Detach();
    return false;
  }
  memcpy(static_cast<std::byte*>(mappingView_) + control_->manifestOffset, manifest.data(),
         manifest.size());
  localModule_ = LoadLibraryExW(payloadPath_.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!localModule_)
  {
    error = L"Windows refused to load the hook payload.";
    Detach();
    return false;
  }
  auto procedure = reinterpret_cast<HookProc>(GetProcAddress(localModule_, "TtrCallWndProcHook"));
  if (!procedure)
  {
    error = L"The hook payload export is missing.";
    Detach();
    return false;
  }
  hook_ = SetWindowsHookExW(WH_CALLWNDPROC, procedure, localModule_, info.threadId);
  if (!hook_)
  {
    error = L"Unable to install the thread-specific Explorer hook.";
    Detach();
    return false;
  }
  taskbar_ = info.taskbar;
  threadId_ = info.threadId;
  explorerPid_ = info.processId;
  if (!CommandAndWait(Command::Initialize, 0, 1000, error))
  {
    Detach();
    return false;
  }
  return true;
}

bool HookLoader::CommandAndWait(Command command, LONG argument, DWORD timeout,
                                std::wstring& error) noexcept
{
  if (!control_ || !taskbar_)
  {
    error = L"There is no attached Explorer session.";
    return false;
  }
  InterlockedExchange(&control_->commandArgument, argument);
  InterlockedExchange(&control_->command, static_cast<LONG>(command));
  LONG sequence = InterlockedIncrement(&control_->commandSequence);
  DWORD_PTR ignored{};
  if (!SendMessageTimeoutW(taskbar_, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeout,
                           &ignored))
  {
    error = L"Explorer did not acknowledge the command.";
    return false;
  }
  const ULONGLONG start = GetTickCount64();
  while (InterlockedCompareExchange(&control_->acknowledgedSequence, 0, 0) < sequence &&
         GetTickCount64() - start < timeout)
    SwitchToThread();
  if (InterlockedCompareExchange(&control_->acknowledgedSequence, 0, 0) < sequence)
  {
    error = L"The hook payload did not acknowledge the command.";
    return false;
  }
  auto state = static_cast<PayloadState>(InterlockedCompareExchange(&control_->payloadState, 0, 0));
  if (state == PayloadState::Faulted || state == PayloadState::Unsupported)
  {
    error = L"The hook payload rejected this taskbar version.";
    return false;
  }
  return true;
}

bool HookLoader::Detach() noexcept
{
  if (control_ && hook_)
  {
    std::wstring ignored;
    if (!CommandAndWait(Command::PrepareUnload, 0, 250, ignored) && ProcessIsAlive(explorerPid_))
      return false;
  }
  if (hook_ && !UnhookWindowsHookEx(hook_))
    return false;
  hook_ = nullptr;
  if (localModule_)
    FreeLibrary(localModule_);
  localModule_ = nullptr;
  if (mappingView_)
    UnmapViewOfFile(mappingView_);
  mappingView_ = nullptr;
  control_ = nullptr;
  if (mapping_)
    CloseHandle(mapping_);
  mapping_ = nullptr;
  taskbar_ = nullptr;
  threadId_ = 0;
  explorerPid_ = 0;
  return true;
}

PayloadState HookLoader::state() const noexcept
{
  return control_
             ? static_cast<PayloadState>(InterlockedCompareExchange(&control_->payloadState, 0, 0))
             : PayloadState::Unloaded;
}
} // namespace ttr::host
