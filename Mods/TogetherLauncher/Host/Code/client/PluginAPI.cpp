#include <Services/PluginService.h>

// Looked up by SKSE plugins with GetProcAddress(GetModuleHandleW(nullptr), "RequestPluginAPI").
// ExeLoader::Load restores the export directory so the main module still exposes it.
extern "C" __declspec(dllexport) void* RequestPluginAPI(SkyrimTogetherAPI::InterfaceVersion aVersion)
{
    switch (aVersion)
    {
    case SkyrimTogetherAPI::InterfaceVersion::V1: return static_cast<SkyrimTogetherAPI::IVSkyrimTogether1*>(&PluginService::Get());
    case SkyrimTogetherAPI::InterfaceVersion::V2: return static_cast<SkyrimTogetherAPI::IVSkyrimTogether2*>(&PluginService::Get());
    }

    return nullptr;
}
