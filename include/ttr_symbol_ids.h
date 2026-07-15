#pragma once
#include <cstdint>

namespace ttr
{
enum class SymbolId : std::uint16_t
{
  Invalid = 0,
  TaskListWnd_Vtable_ITaskListUI = 1,
  TaskListThumbnailWnd_GetHoverIndex,
  TaskListThumbnailWnd_GetTaskItem,
  TaskListThumbnailWnd_GetTaskGroup,
  TaskListThumbnailWnd_TaskReordered,
  TaskListThumbnailWnd_WndProc,
  TaskGroup_GetNumItems,
  TaskListWnd_GetTBGroupFromGroup,
  TaskBtnGroup_GetGroupType,
  TaskBtnGroup_IndexOfTaskItem,
  TaskListWnd_TaskInclusionChanged,
  TaskItemFilter_IsTaskAllowed,
  TaskItemThumbnail_ConstructorV1,
  TaskItemThumbnail_ConstructorV2,
  TaskGroup_Thumbnails,
  TaskItemThumbnailVector_Size,
  TaskItemThumbnailVector_GetAt,
  TaskListWnd_HandleExtendedUIClick,
  HoverFlyoutModel_TargetItemKey,
  TaskItemThumbnailList_OnPointerMoved,
  FlyoutFrame_UpdateFlyoutPosition,
  Dpa_GetPtr,
  Last = Dpa_GetPtr
};
} // namespace ttr
