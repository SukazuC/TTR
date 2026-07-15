#pragma once
#include <Windows.h>
#include <vector>
namespace ttr::payload
{
class HookTransaction
{
public:
  bool Begin() noexcept;
  bool Add(void*, void*, void**) noexcept;
  bool Commit() noexcept;
  void Reset() noexcept;
  size_t count() const noexcept
  {
    return targets_.size();
  }

private:
  bool initialized_{};
  std::vector<void*> targets_;
};
} // namespace ttr::payload
