#pragma once
#include "ttr_array_move.h"
#include <span>
#include <vector>

namespace ttr::payload
{
struct PointerMove
{
  std::span<void*> items;
  std::size_t from{};
  std::size_t to{};
};

inline void RollbackPointerMoves(const std::span<const PointerMove> moves,
                                 std::size_t applied) noexcept
{
  while (applied)
  {
    const auto& move = moves[--applied];
    ttr::MovePointer(move.items, move.to, move.from);
  }
}

inline bool ApplyPointerMoves(const std::span<const PointerMove> moves) noexcept
{
  std::size_t applied{};
  for (const auto& move : moves)
  {
    if (!ttr::MovePointer(move.items, move.from, move.to))
    {
      RollbackPointerMoves(moves, applied);
      return false;
    }
    ++applied;
  }
  return true;
}
} // namespace ttr::payload
