#pragma once

// Locates game classes through the MSVC RTTI the game ships with, by mangled type
// name (".?AVTweenMenuCameraState@@"). This needs no address library id, which
// makes it usable on 1.5.x where the AE id map has gaps.
namespace RttiScan
{
// Primary vtable of the class, or null when the type is not found.
void** FindVTable(const char* acpMangledName) noexcept;
} // namespace RttiScan
