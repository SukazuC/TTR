#pragma once
#include "manifest_runtime.h"
#include "ttr_protocol.h"
#include <Windows.h>
#include <atomic>

namespace ttr::payload
{
struct State
{
  HMODULE instance{};
  HANDLE mapping{};
  void* view{};
  TtrSessionControlV1* control{};
  Compatibility compatibility{};
  std::atomic_bool enabled{false};
  std::atomic_bool faulted{false};
  std::atomic_long activeCallbacks{0};
  LONG lastCommand{};
};
class CallbackScope
{
public:
  CallbackScope() noexcept;
  ~CallbackScope() noexcept;
  CallbackScope(const CallbackScope&) = delete;
  CallbackScope& operator=(const CallbackScope&) = delete;
};
State& GetState() noexcept;
bool EnsureSession() noexcept;
void ProcessCommand() noexcept;
void MarkFault(PayloadError) noexcept;
} // namespace ttr::payload
