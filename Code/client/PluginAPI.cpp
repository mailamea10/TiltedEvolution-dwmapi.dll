#include <Services/PluginService.h>

// Looked up by SKSE plugins with GetProcAddress(GetModuleHandleW(nullptr), "RequestPluginAPI").
// ExeLoader::Load restores the export directory so the main module still exposes it.
extern "C" __declspec(dllexport) void* RequestPluginAPI(SkyrimTogetherAPI::InterfaceVersion aVersion)
{
    if (aVersion != SkyrimTogetherAPI::InterfaceVersion::V1)
        return nullptr;

    return static_cast<SkyrimTogetherAPI::IVSkyrimTogether1*>(&PluginService::Get());
}
