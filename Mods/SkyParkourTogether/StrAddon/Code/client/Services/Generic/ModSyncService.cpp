#include <Services/ModSyncService.h>

#include <ModSync/STRModSyncAPI.h>

#include <Components.h>
#include <World.h>

#include <Events/UpdateEvent.h>
#include <Messages/NotifyScriptAnimation.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Services/TransportService.h>

#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/Animation/IAnimationGraphManagerHolder.h>
#include <Misc/BSFixedString.h>

#include <psapi.h>

#include <cstdlib>
#include <cstring>

// Lets a plugin see that this Skyrim Together build carries the add-on.
extern "C" __declspec(dllexport) uint32_t STRModSync_HostApiVersion()
{
    return STRModSync::kApiVersion;
}

namespace
{
// "STRMS1|<sender's server id, hex>|<event>|<type>:<name>:<value>|...", relayed as a script animation for an
// object id that the stock client drops without playing anything. 1.8.5 resolves that id as a server id, so
// one that cannot exist is used; 1.8.3 skips id 0 outright (apply_addon.py --protocol 1.8.3 defines it).
#ifndef STR_MODSYNC_NO_OBJECT
#define STR_MODSYNC_NO_OBJECT 0xFFFFFFFFu
#endif
constexpr const char* kWirePrefix = "STRMS1|";
constexpr uint32_t kNoObject = STR_MODSYNC_NO_OBJECT;
constexpr size_t kMaxWireLength = 480;

// Plugins can load late (some are loaded by other plugins), so keep looking for a while.
constexpr double kScanInterval = 5.0;
constexpr double kScanDuration = 600.0;

struct OwnedVariable
{
    String Name;
    char Type;
};

struct OwnedSource
{
    String Prefix;
    Vector<OwnedVariable> Variables;
};

struct CapturedEvent
{
    String Event;
    String Variables; // already encoded, each starting with '|'
};

std::mutex s_lock;
Vector<OwnedSource> s_sources;
Vector<CapturedEvent> s_captured;
Set<uintptr_t> s_seenModules;

bool StartsWithNoCase(const char* acpText, const char* acpPrefix) noexcept
{
    return _strnicmp(acpText, acpPrefix, strlen(acpPrefix)) == 0;
}

bool IsWireSafe(const char* acpText) noexcept
{
    if (!acpText || !*acpText)
        return false;
    for (const char* p = acpText; *p; ++p)
    {
        if (*p == '|' || static_cast<unsigned char>(*p) < 0x20)
            return false;
    }
    return strlen(acpText) < 128;
}

const OwnedSource* FindSource(const char* acpEvent) noexcept
{
    for (const auto& source : s_sources)
    {
        if (StartsWithNoCase(acpEvent, source.Prefix.c_str()))
            return &source;
    }
    return nullptr;
}

using TNotify = bool (*)(IAnimationGraphManagerHolder* apThis, BSFixedString* apEvent);
TNotify s_realNotify = nullptr;

bool HookPlayerNotify(IAnimationGraphManagerHolder* apThis, BSFixedString* apEvent)
{
    const bool cResult = s_realNotify(apThis, apEvent);

    // Only what the player's own graph took: a refused event did not animate here either.
    if (cResult && apEvent && apEvent->data)
        ModSyncService::Capture(apEvent->data);

    return cResult;
}
} // namespace

ModSyncService::ModSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&ModSyncService::OnUpdate>(this);
    m_notifyConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&ModSyncService::OnNotifyScriptAnimation>(this);
}

void ModSyncService::ScanForPlugins() noexcept
{
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return;

    const size_t cCount = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));
    for (size_t i = 0; i < cCount; ++i)
    {
        const auto cKey = reinterpret_cast<uintptr_t>(modules[i]);
        {
            std::lock_guard guard(s_lock);
            if (s_seenModules.count(cKey))
                continue;
            s_seenModules.insert(cKey);
        }

        const auto getDescriptor = reinterpret_cast<STRModSync::TGetDescriptor>(GetProcAddress(modules[i], STRModSync::kDescriptorExport));
        if (!getDescriptor)
            continue;

        char path[MAX_PATH]{};
        K32GetModuleBaseNameA(GetCurrentProcess(), modules[i], path, MAX_PATH);

        const STRModSync::Descriptor* pDescriptor = getDescriptor();
        if (!pDescriptor || pDescriptor->ApiVersion != STRModSync::kApiVersion || !pDescriptor->Sources || pDescriptor->SourceCount == 0 ||
            pDescriptor->SourceCount > STRModSync::kMaxSources)
        {
            spdlog::warn("mod sync: {} has a descriptor this add-on cannot use (API {}, add-on API {})", path, pDescriptor ? pDescriptor->ApiVersion : 0,
                         STRModSync::kApiVersion);
            continue;
        }

        Vector<OwnedSource> sources;
        for (uint32_t s = 0; s < pDescriptor->SourceCount; ++s)
        {
            const auto& source = pDescriptor->Sources[s];
            if (!IsWireSafe(source.EventPrefix) || source.VariableCount > STRModSync::kMaxVariablesPerSource || (source.VariableCount && !source.Variables))
                continue;

            OwnedSource owned;
            owned.Prefix = source.EventPrefix;
            for (uint32_t v = 0; v < source.VariableCount; ++v)
            {
                const auto& variable = source.Variables[v];
                const char cType = variable.Type;
                if (IsWireSafe(variable.Name) && !strchr(variable.Name, ':') && (cType == STRModSync::kInt || cType == STRModSync::kBool || cType == STRModSync::kFloat))
                    owned.Variables.push_back({variable.Name, cType});
            }
            sources.push_back(std::move(owned));
        }

        const char* pRequired = pDescriptor->RequiredModule;
        const bool cRequiredLoaded = !pRequired || GetModuleHandleA(pRequired) != nullptr;
        spdlog::info("mod sync: {} {} ({}) registered {} event source(s){}", pDescriptor->ModName ? pDescriptor->ModName : "?",
                     pDescriptor->ModVersion ? pDescriptor->ModVersion : "", path, sources.size(),
                     cRequiredLoaded ? "" : fmt::format(", but {} is not loaded", pRequired).c_str());

        std::lock_guard guard(s_lock);
        for (auto& source : sources)
            s_sources.push_back(std::move(source));
    }
}

void ModSyncService::Capture(const char* acpEvent) noexcept
{
    OwnedSource source;
    {
        std::lock_guard guard(s_lock);
        const OwnedSource* pSource = FindSource(acpEvent);
        if (!pSource || !IsWireSafe(acpEvent))
            return;
        source = *pSource;
    }

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    CapturedEvent captured;
    captured.Event = acpEvent;

    // Read the variables now, while they hold what the mod set for this event.
    for (const auto& variable : source.Variables)
    {
        BSFixedString name(variable.Name.c_str());
        String value;
        switch (variable.Type)
        {
        case STRModSync::kInt:
        {
            uint32_t intValue = 0;
            if (pPlayer->animationGraphHolder.GetVariableInt(&name, &intValue))
                value = std::to_string(static_cast<int32_t>(intValue)).c_str();
            break;
        }
        case STRModSync::kBool:
        {
            bool boolValue = false;
            if (pPlayer->animationGraphHolder.GetVariableBool(&name, &boolValue))
                value = boolValue ? "1" : "0";
            break;
        }
        case STRModSync::kFloat:
        {
            float floatValue = 0.f;
            if (pPlayer->animationGraphHolder.GetVariableFloat(&name, &floatValue))
                value = fmt::format("{:.3f}", floatValue).c_str();
            break;
        }
        }
        name.Release();

        // A variable the graph does not have (another version of the mod) is simply not sent.
        if (!value.empty())
            captured.Variables += fmt::format("|{}:{}:{}", variable.Type, variable.Name.c_str(), value.c_str()).c_str();
    }

    std::lock_guard guard(s_lock);
    s_captured.push_back(std::move(captured));
}

void ModSyncService::InstallHook() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    m_hookInstalled = true;

    // The player's own IAnimationGraphManagerHolder vtable. The mod being synced may hook the same slot;
    // whichever of us comes second calls the other, so the order does not matter.
    auto** pVTable = *reinterpret_cast<void***>(&pPlayer->animationGraphHolder);
    constexpr size_t kNotifySlot = 1;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&pVTable[kNotifySlot], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        spdlog::error("mod sync: the player's graph notify could not be hooked");
        return;
    }
    s_realNotify = reinterpret_cast<TNotify>(pVTable[kNotifySlot]);
    pVTable[kNotifySlot] = reinterpret_cast<void*>(&HookPlayerNotify);
    VirtualProtect(&pVTable[kNotifySlot], sizeof(void*), oldProtect, &oldProtect);

    spdlog::info("mod sync: watching the player's graph events");
}

void ModSyncService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_scanElapsed < kScanDuration)
    {
        m_scanElapsed += acEvent.Delta;
        m_scanTimer -= acEvent.Delta;
        if (m_scanTimer <= 0.0)
        {
            m_scanTimer = kScanInterval;
            ScanForPlugins();
        }
    }

    bool anySources;
    {
        std::lock_guard guard(s_lock);
        anySources = !s_sources.empty();
    }
    if (anySources && !m_hookInstalled)
        InstallHook();

    SendCaptured();
}

void ModSyncService::SendCaptured() noexcept
{
    Vector<CapturedEvent> captured;
    {
        std::lock_guard guard(s_lock);
        if (s_captured.empty())
            return;
        captured.swap(s_captured);
    }

    if (!m_transport.IsOnline())
        return;

    auto view = m_world.view<FormIdComponent>();
    const auto it = std::find_if(view.begin(), view.end(), [view](auto entity) { return view.get<FormIdComponent>(entity).Id == 0x14; });
    if (it == view.end())
        return;

    const auto serverId = Utils::GetServerId(*it);
    if (!serverId)
        return;

    for (const auto& event : captured)
    {
        String wire = fmt::format("{}{:X}|{}{}", kWirePrefix, *serverId, event.Event.c_str(), event.Variables.c_str()).c_str();
        if (wire.size() > kMaxWireLength)
            wire = fmt::format("{}{:X}|{}", kWirePrefix, *serverId, event.Event.c_str()).c_str();

        ScriptAnimationRequest request{};
        request.FormID = kNoObject;
        request.EventName = wire;
        m_transport.Send(request);

        spdlog::debug("mod sync: sent '{}'", wire.c_str());
    }
}

void ModSyncService::OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept
{
    const char* pWire = acMessage.EventName.c_str();
    if (!StartsWithNoCase(pWire, kWirePrefix))
        return;

    // Split into "<server id>", "<event>", then the variables.
    Vector<std::string> parts;
    {
        std::string current;
        for (const char* p = pWire + strlen(kWirePrefix); *p; ++p)
        {
            if (*p == '|')
            {
                parts.push_back(std::move(current));
                current.clear();
            }
            else
                current += *p;
        }
        parts.push_back(std::move(current));
    }
    if (parts.size() < 2)
        return;

    const uint32_t cServerId = static_cast<uint32_t>(strtoul(parts[0].c_str(), nullptr, 16));
    const std::string& cEvent = parts[1];

    const auto entity = Utils::FindEntityByServerId(cServerId);
    if (!entity || m_world.try_get<LocalComponent>(*entity))
        return;

    auto* pActor = Utils::GetByServerId<Actor>(cServerId);
    if (!pActor)
        return;

    // Only what this client's own copy of the mod registered.
    OwnedSource source;
    {
        std::lock_guard guard(s_lock);
        const OwnedSource* pSource = FindSource(cEvent.c_str());
        if (!pSource)
        {
            spdlog::debug("mod sync: '{}' is not registered here, ignored", cEvent);
            return;
        }
        source = *pSource;
    }

    // Variables first, so the copy picks the same animation the owner played.
    for (size_t i = 2; i < parts.size(); ++i)
    {
        const std::string& cSpec = parts[i];
        const auto cFirst = cSpec.find(':');
        const auto cLast = cSpec.rfind(':');
        if (cFirst != 1 || cLast == std::string::npos || cLast <= cFirst + 1)
            continue;

        const char cType = cSpec[0];
        const std::string cName = cSpec.substr(cFirst + 1, cLast - cFirst - 1);
        const char* pcValue = cSpec.c_str() + cLast + 1;

        const bool cKnown = std::any_of(source.Variables.begin(), source.Variables.end(),
                                        [&](const OwnedVariable& acVariable) { return acVariable.Type == cType && _stricmp(acVariable.Name.c_str(), cName.c_str()) == 0; });
        if (!cKnown)
            continue;

        BSFixedString variable(cName.c_str());
        auto& holder = pActor->animationGraphHolder;
        if (cType == STRModSync::kInt)
            holder.SetVariableInt(&variable, static_cast<int32_t>(strtol(pcValue, nullptr, 10)));
        else if (cType == STRModSync::kBool)
            holder.SetVariableBool(&variable, *pcValue == '1');
        else if (cType == STRModSync::kFloat)
            holder.SetVariableFloat(&variable, strtof(pcValue, nullptr));
        variable.Release();
    }

    BSFixedString eventName(cEvent.c_str());
    const bool cPlayed = pActor->animationGraphHolder.SendAnimationEvent(&eventName);
    eventName.Release();

    spdlog::debug("mod sync: '{}' on {:X} {}", cEvent, pActor->formID, cPlayed ? "played" : "was refused");
}
