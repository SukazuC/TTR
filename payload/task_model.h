#pragma once
#include "manifest_runtime.h"
#include <Windows.h>
namespace ttr::payload
{
bool ConfigureTaskModel(const Compatibility&) noexcept;
bool MoveTaskInGroup(void*, void*, void*) noexcept;
void SetFilterGate(bool) noexcept;
bool FilterGate() noexcept;
void CaptureDpa(void*) noexcept;
bool ShouldCaptureDpa() noexcept;
} // namespace ttr::payload
