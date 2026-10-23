#pragma once

#include <cstdint>

namespace TiltedPhoques { namespace Platform {
// Safe cross-platform DLL loading
#if defined(_WIN32)
extern "C" void* SafeLoadLibrary(const char* lpLibFileName);
extern "C" void FreeLibrary(void* hModule);
extern "C" void* GetProcAddress(void* hModule, const char* procName);
#else // Linux/Unix fallback using dlopen/dlsym
#include <dlfcn.h>
#endif

inline void* SafeLoadLibrary(const char* lpLibFileName) noexcept {
if (!lpLibFileName || strlen(lpLibFileName) == 0) return nullptr;
#if defined(_WIN32)
return LoadLibraryA(lpLibFileName);
#else
void* handle = dlopen(lpLibFileName, RTLD_NOW | RTLD_LOCAL);
return handle;
#endif
}

inline void* GetProcAddress(void* hModule, const char* procName) noexcept {
if (!hModule || !procName) return nullptr;
#if defined(_WIN32)
return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(hModule), procName));
#else
void* func = dlsym(reinterpret_cast<dlsym_handle_t>(hModule), procName);
return func;
#endif
}

inline void FreeLibrary(void* hModule) noexcept {
if (!hModule) return;
#if defined(_WIN32)
FreeLibrary(static_cast<HMODULE>(hModule));
#else
dlclose(hModule);
#endif
}

} // namespace Platform } // namespace TiltedPhoques