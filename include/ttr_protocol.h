#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace ttr {
inline constexpr char kSessionMagic[8] = {'T','T','R','S','E','S','1','\0'};
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kSessionMappingBytes = 96 * 1024;
inline constexpr std::uint32_t kLogRingBytes = 64 * 1024;

enum class Command : LONG { None, Initialize, Enable, Disable, QueryStatus, PrepareUnload, SetDebugLogging };
enum class PayloadState : LONG { Unloaded, Initializing, Disabled, Active, Unsupported, Faulted, Unloading };
enum class Backend : LONG { None, Classic, Xaml };
enum class PayloadError : LONG {
  None, BadControlBlock, HostMismatch, BadManifest, UnsupportedModules,
  HookInitializeFailed, HookCreateFailed, HookEnableFailed, CallbackDrainTimeout,
  StructuredException, InternalError
};

#pragma pack(push, 1)
struct TtrSessionControlV1 {
  char magic[8];
  std::uint32_t byteSize;
  std::uint32_t protocolVersion;
  std::uint32_t hostPid;
  std::uint32_t explorerPid;
  std::uint64_t sessionNonce;
  volatile LONG commandSequence;
  volatile LONG acknowledgedSequence;
  volatile LONG command;
  volatile LONG commandArgument;
  volatile LONG payloadState;
  volatile LONG payloadError;
  volatile LONG activeThumbnailBackend;
  volatile LONG installedHookCount;
  std::uint32_t manifestOffset;
  std::uint32_t manifestSize;
  std::uint32_t logOffset;
  std::uint32_t logSize;
  volatile LONG logWriteOffset;
  volatile LONG logDroppedCount;
};

struct PayloadBootstrapV1 {
  std::uint32_t byteSize;
  std::uint32_t protocolVersion;
  std::uint32_t hostPid;
  std::uint32_t explorerPid;
  std::uint64_t sessionNonce;
};
#pragma pack(pop)

static_assert(sizeof(TtrSessionControlV1) == 88);
static_assert(sizeof(PayloadBootstrapV1) == 24);
static_assert(offsetof(TtrSessionControlV1, commandSequence) == 32);
}
