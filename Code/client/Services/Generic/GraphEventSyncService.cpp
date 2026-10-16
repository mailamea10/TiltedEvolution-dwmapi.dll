#include <Services/GraphEventSyncService.h>

#include <Components.h>
#include <World.h>

#include <Events/UpdateEvent.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Services/TransportService.h>

#include <PlayerCharacter.h>
#include <Games/Animation/IAnimationGraphManagerHolder.h>
#include <Misc/BSFixedString.h>

namespace
{
struct GraphVariable
{
    enum Type : char
    {
        kInt = 'i',
        kBool = 'b',
        kFloat = 'f',
    };

    const char* Name;
    Type VariableType;
};

// The mods whose player graph events are synced, and the graph variables their animations read. Each
// variable is sent ahead of the event, so the other player's copy picks the same animation.
// SkyParkour (NG v2/v3): "SkyParkour", "SkyParkour_Slide", "_SlideToSneak", "_Interrupt", "_Stop", ...
const GraphVariable kSkyParkourVariables[] = {
    {"SkyParkourLedge", GraphVariable::kInt},          // vault, climb, grab or step and its height
    {"SkyParkourGrabVariant", GraphVariable::kBool},   // low or high grab
    {"SkyParkourLowerBody", GraphVariable::kBool},     // steps with a weapon out play on the legs only
    {"SkyParkourIsLandingRoll", GraphVariable::kBool}, // slide or landing roll
    {"SkyParkourSpeedMult", GraphVariable::kFloat},    // playback speed setting
};

struct GraphEventSource
{
    const char* EventPrefix;
    const GraphVariable* Variables;
    size_t VariableCount;
};

const GraphEventSource kSources[] = {
    {"SkyParkour", kSkyParkourVariables, std::size(kSkyParkourVariables)},
};

struct CapturedEvent
{
    String Event;
    Vector<String> Variables; // already encoded as "STRGV:<type>:<name>:<value>"
};

std::mutex s_lock;
Vector<CapturedEvent> s_captured;

using TNotify = bool (*)(IAnimationGraphManagerHolder* apThis, BSFixedString* apEvent);
TNotify s_realNotify = nullptr;

bool StartsWith(const char* acpText, const char* acpPrefix) noexcept
{
    return _strnicmp(acpText, acpPrefix, strlen(acpPrefix)) == 0;
}

bool HookPlayerNotify(IAnimationGraphManagerHolder* apThis, BSFixedString* apEvent)
{
    const bool cResult = s_realNotify(apThis, apEvent);

    // Only what the player's own graph took: a refused event did not animate here either.
    if (cResult && apEvent && apEvent->data)
        GraphEventSyncService::Capture(apEvent->data);

    return cResult;
}
} // namespace

GraphEventSyncService::GraphEventSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&GraphEventSyncService::OnUpdate>(this);
}

void GraphEventSyncService::Capture(const char* acpEvent) noexcept
{
    for (const auto& source : kSources)
    {
        if (!StartsWith(acpEvent, source.EventPrefix))
            continue;

        auto* pPlayer = PlayerCharacter::Get();
        if (!pPlayer)
            return;

        CapturedEvent captured;
        captured.Event = acpEvent;

        // Read the variables now, while they hold what the mod set for this event.
        for (size_t i = 0; i < source.VariableCount; ++i)
        {
            const auto& variable = source.Variables[i];
            BSFixedString name(variable.Name);
            String value;
            switch (variable.VariableType)
            {
            case GraphVariable::kInt:
            {
                uint32_t intValue = 0;
                if (pPlayer->animationGraphHolder.GetVariableInt(&name, &intValue))
                    value = std::to_string(static_cast<int32_t>(intValue)).c_str();
                break;
            }
            case GraphVariable::kBool:
            {
                bool boolValue = false;
                if (pPlayer->animationGraphHolder.GetVariableBool(&name, &boolValue))
                    value = boolValue ? "1" : "0";
                break;
            }
            case GraphVariable::kFloat:
            {
                float floatValue = 0.f;
                if (pPlayer->animationGraphHolder.GetVariableFloat(&name, &floatValue))
                    value = fmt::format("{:.2f}", floatValue).c_str();
                break;
            }
            }
            name.Release();

            // A variable the graph does not have (another version of the mod) is simply not sent.
            if (!value.empty())
                captured.Variables.push_back(fmt::format("{}{}:{}:{}", kVariablePrefix, static_cast<char>(variable.VariableType), variable.Name, value.c_str()).c_str());
        }

        std::lock_guard guard(s_lock);
        s_captured.push_back(std::move(captured));
        return;
    }
}

void GraphEventSyncService::InstallHook() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    m_hookInstalled = true;

    // The player's own IAnimationGraphManagerHolder vtable; SkyParkour hooks the same slot, and whichever of
    // us comes second calls the other, so the order does not matter.
    auto** pVTable = *reinterpret_cast<void***>(&pPlayer->animationGraphHolder);
    constexpr size_t kNotifySlot = 1;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&pVTable[kNotifySlot], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        spdlog::error("graph event sync: the player's graph notify could not be hooked");
        return;
    }
    s_realNotify = reinterpret_cast<TNotify>(pVTable[kNotifySlot]);
    pVTable[kNotifySlot] = reinterpret_cast<void*>(&HookPlayerNotify);
    VirtualProtect(&pVTable[kNotifySlot], sizeof(void*), oldProtect, &oldProtect);

    spdlog::info("graph event sync: watching the player's graph events (SkyParkour)");
}

void GraphEventSyncService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_hookInstalled)
        InstallHook();

    Vector<CapturedEvent> captured;
    {
        std::lock_guard guard(s_lock);
        if (s_captured.empty())
            return;
        captured.swap(s_captured);
    }

    if (!m_transport.IsConnected())
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
        // Variables first: the relay keeps the order, so they are set before the event plays.
        for (const auto& variable : event.Variables)
        {
            ScriptAnimationRequest request{};
            request.FormID = *serverId;
            request.EventName = variable;
            m_transport.Send(request);
        }

        ScriptAnimationRequest request{};
        request.FormID = *serverId;
        request.EventName = event.Event;
        m_transport.Send(request);

        spdlog::debug("graph event sync: sent '{}' with {} variables", event.Event.c_str(), event.Variables.size());
    }
}
