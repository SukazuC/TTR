#include "payload_state.h"
#include "classic_backend.h"
#include "xaml_backend.h"
#include <cstring>

namespace ttr::payload
{
namespace
{
State state;
void SetResult(PayloadState s, PayloadError e = PayloadError::None)
{
  InterlockedExchange(&state.control->payloadError, static_cast<LONG>(e));
  InterlockedExchange(&state.control->payloadState, static_cast<LONG>(s));
}
} // namespace
State& GetState() noexcept
{
  return state;
}
CallbackScope::CallbackScope() noexcept
{
  state.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
}
CallbackScope::~CallbackScope() noexcept
{
  const long previous = state.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
  if (previous <= 0)
  {
    state.activeCallbacks.store(0, std::memory_order_release);
    MarkFault(PayloadError::InternalError);
  }
}
bool EnsureSession() noexcept
{
  if (state.control)
    return true;
  wchar_t name[96]{};
  swprintf_s(name, L"Local\\TaskbarThumbnailReorder.Session.%lu", GetCurrentProcessId());
  state.mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
  if (!state.mapping)
    return false;
  state.view = MapViewOfFile(state.mapping, FILE_MAP_ALL_ACCESS, 0, 0, kSessionMappingBytes);
  if (!state.view)
  {
    CloseHandle(state.mapping);
    state.mapping = nullptr;
    return false;
  }
  auto reset = []() {
    if (state.view)
      UnmapViewOfFile(state.view);
    if (state.mapping)
      CloseHandle(state.mapping);
    state.view = nullptr;
    state.mapping = nullptr;
  };
  auto* c = static_cast<TtrSessionControlV1*>(state.view);
  const bool ranges = CheckedRange(c->manifestOffset, c->manifestSize, kSessionMappingBytes) &&
                      CheckedRange(c->logOffset, c->logSize, kSessionMappingBytes);
  const std::uint64_t manifestEnd = static_cast<std::uint64_t>(c->manifestOffset) + c->manifestSize;
  const bool separate = manifestEnd <= c->logOffset ||
                        static_cast<std::uint64_t>(c->logOffset) + c->logSize <= c->manifestOffset;
  if (std::memcmp(c->magic, kSessionMagic, 8) || c->byteSize != sizeof(*c) ||
      c->protocolVersion != kProtocolVersion || c->explorerPid != GetCurrentProcessId() ||
      !c->hostPid || !c->sessionNonce || c->manifestSize < sizeof(ManifestHeaderV2) ||
      !c->logSize || !ranges || !separate)
  {
    reset();
    return false;
  }
  HANDLE host = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, c->hostPid);
  if (!host || WaitForSingleObject(host, 0) != WAIT_TIMEOUT)
  {
    if (host)
      CloseHandle(host);
    reset();
    return false;
  }
  DWORD hostSession{}, currentSession{};
  const bool sameSession = ProcessIdToSessionId(c->hostPid, &hostSession) &&
                           ProcessIdToSessionId(GetCurrentProcessId(), &currentSession) &&
                           hostSession == currentSession;
  CloseHandle(host);
  if (!sameSession)
  {
    reset();
    return false;
  }
  state.control = c;
  return true;
}
void MarkFault(PayloadError e) noexcept
{
  state.enabled = false;
  state.faulted = true;
  if (state.control)
    SetResult(PayloadState::Faulted, e);
}
void ProcessCommand() noexcept
{
  if (!EnsureSession())
    return;
  LONG sequence = InterlockedCompareExchange(&state.control->commandSequence, 0, 0);
  if (sequence == state.lastCommand)
    return;
  state.lastCommand = sequence;
  auto command = static_cast<Command>(InterlockedCompareExchange(&state.control->command, 0, 0));
  switch (command)
  {
  case Command::Initialize: {
    SetResult(PayloadState::Initializing);
    auto* b = static_cast<const std::byte*>(state.view) + state.control->manifestOffset;
    if (!ResolveCompatibility({b, state.control->manifestSize}, state.compatibility))
      SetResult(PayloadState::Unsupported, PayloadError::UnsupportedModules);
    else
      SetResult(PayloadState::Disabled);
    break;
  }
  case Command::Enable: {
    if (state.faulted.load(std::memory_order_acquire))
    {
      SetResult(PayloadState::Faulted, PayloadError::InternalError);
      break;
    }
    if (state.enabled.load(std::memory_order_acquire))
    {
      SetResult(PayloadState::Active);
      break;
    }
    bool classic = false, xaml = false;
    if (state.compatibility.backendFlags & BackendXaml)
      xaml = EnableXaml(state.compatibility);
    if (!xaml && (state.compatibility.backendFlags & BackendClassic))
      classic = EnableClassic(state.compatibility);
    if (classic || xaml)
    {
      state.enabled.store(true, std::memory_order_release);
      SetResult(PayloadState::Active);
      InterlockedExchange(&state.control->activeThumbnailBackend,
                          xaml ? static_cast<LONG>(Backend::Xaml)
                               : static_cast<LONG>(Backend::Classic));
    }
    else
    {
      DisableClassic();
      DisableXaml();
      MarkFault(PayloadError::HookCreateFailed);
    }
    break;
  }
  case Command::Disable:
  case Command::PrepareUnload: {
    state.enabled.store(false, std::memory_order_release);
    ULONGLONG start = GetTickCount64();
    while (state.activeCallbacks.load(std::memory_order_acquire) != 0 &&
           GetTickCount64() - start < 200)
      SwitchToThread();
    if (state.activeCallbacks.load(std::memory_order_acquire) != 0)
    {
      MarkFault(PayloadError::CallbackDrainTimeout);
      break;
    }
    DisableClassic();
    DisableXaml();
    InterlockedExchange(&state.control->activeThumbnailBackend, static_cast<LONG>(Backend::None));
    SetResult(command == Command::PrepareUnload ? PayloadState::Unloading : PayloadState::Disabled);
    break;
  }
  case Command::QueryStatus:
  case Command::SetDebugLogging:
  case Command::None:
    break;
  }
  InterlockedExchange(&state.control->acknowledgedSequence, sequence);
}
} // namespace ttr::payload
