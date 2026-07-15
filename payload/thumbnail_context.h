#pragma once
#include <cstdint>

namespace ttr::payload
{
struct ThumbnailContext
{
  void* taskGroup{};
  void* taskItem{};
  void* taskListUi{};
  std::uint64_t generation{};
};

inline bool SameReorderContext(const ThumbnailContext& source, const ThumbnailContext& target,
                               const std::uint64_t activeGeneration) noexcept
{
  return activeGeneration != 0 && source.generation == activeGeneration &&
         target.generation == activeGeneration && source.taskGroup &&
         source.taskGroup == target.taskGroup && source.taskItem && target.taskItem &&
         source.taskItem != target.taskItem && source.taskListUi &&
         source.taskListUi == target.taskListUi;
}
} // namespace ttr::payload
