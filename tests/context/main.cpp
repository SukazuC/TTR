#include "reorder_transaction.h"
#include "thumbnail_context.h"

#include <array>
#include <iostream>

namespace
{
int failures{};

void Check(bool value, const char* name)
{
  if (!value)
  {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

void ContextCases()
{
  using ttr::payload::SameReorderContext;
  using ttr::payload::ThumbnailContext;
  int group{}, from{}, to{}, ui{}, otherUi{};
  const ThumbnailContext source{&group, &from, &ui, 7};
  const ThumbnailContext target{&group, &to, &ui, 7};
  Check(SameReorderContext(source, target, 7), "constructor mapping shares exact UI context");
  auto invalid = target;
  invalid.taskListUi = &otherUi;
  Check(!SameReorderContext(source, invalid, 7), "cross-taskbar UI context rejected");
  invalid = target;
  invalid.generation = 6;
  Check(!SameReorderContext(source, invalid, 7), "stale generation rejected");
  Check(!SameReorderContext(source, target, 8), "disable invalidates prior mappings");
  invalid = target;
  invalid.taskGroup = nullptr;
  Check(!SameReorderContext(source, invalid, 7), "missing task group rejected");
  invalid = target;
  invalid.taskItem = &from;
  Check(!SameReorderContext(source, invalid, 7), "same item rejected");
}

void TransactionCases()
{
  using ttr::payload::ApplyPointerMoves;
  using ttr::payload::PointerMove;
  using ttr::payload::RollbackPointerMoves;
  int a{}, b{}, c{};
  std::array<void*, 3> group{&a, &b, &c};
  std::array<void*, 3> primary{&a, &b, &c};
  std::array<void*, 3> secondary{&a, &b, &c};
  const std::array<PointerMove, 3> moves{{{group, 0, 2}, {primary, 0, 2}, {secondary, 0, 2}}};
  Check(ApplyPointerMoves(moves), "multi-taskbar transaction applies");
  Check(group[2] == &a && primary[2] == &a && secondary[2] == &a,
        "all taskbar arrays remain synchronized");
  RollbackPointerMoves(moves, moves.size());
  Check(group[0] == &a && primary[0] == &a && secondary[0] == &a,
        "notification failure rollback restores all arrays");

  const auto original = group;
  const std::array<PointerMove, 2> invalidMoves{{{group, 0, 2}, {primary, 9, 0}}};
  Check(!ApplyPointerMoves(invalidMoves), "partial move failure rejected");
  Check(group == original, "partial move failure rolls back prior mutation");
}
} // namespace

int main()
{
  ContextCases();
  TransactionCases();
  return failures ? 1 : 0;
}
