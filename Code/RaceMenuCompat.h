#pragma once

#include <cstdint>
#include "PlatformGuard.h"

namespace TiltedPhoques { namespace RaceMenuIntegration {
struct RaceMenuState final {
bool initialized = false;
void* raceMenuHandle = nullptr;

static void Initialize() noexcept {
if (initialized) return;
#if defined(_WIN32)
raceMenuHandle = Platform::SafeLoadLibrary("RaceMenuPlugin.dll");
if (!raceMenuHandle) { initialized = true; return; }
auto getFuncPtr = Platform::GetProcAddress(raceMenuHandle, "GetProcAddr");
if (!getFuncPtr || !funcCall) { initialized = true; return; }
#endif
initialized = true;
}
};

} // namespace RaceMenuIntegration } // namespace TiltedPhoques