#include <Services/SceneSyncService.h>

#include <Components.h>
#include <World.h>

#include <Events/UpdateEvent.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Services/TransportService.h>

#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Games/Animation/IAnimationGraphManagerHolder.h>
#include <Misc/BSFixedString.h>

#include <intrin.h>

namespace
{
// Plugins whose graph events and placement drive a scene. Matched by file name.
constexpr const wchar_t* kScenePlugins[] = {L"SexLabUtil.dll"};

struct ModuleRange
{
    uintptr_t Begin{};
    uintptr_t End{};
};

std::mutex s_moduleLock;
Vector<ModuleRange> s_modules;
uint64_t s_nextModuleScan = 0;

void ScanModules() noexcept
{
    const uint64_t cNow = GetTickCount64();
    if (cNow < s_nextModuleScan)
        return;
    s_nextModuleScan = cNow + 5000;

    Vector<ModuleRange> modules;
    for (const auto* pName : kScenePlugins)
    {
        const auto hModule = GetModuleHandleW(pName);
        if (!hModule)
            continue;
        const auto* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(hModule);
        const auto* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(hModule) + pDos->e_lfanew);
        const auto cBase = reinterpret_cast<uintptr_t>(hModule);
        modules.push_back({cBase, cBase + pNt->OptionalHeader.SizeOfImage});
    }

    std::lock_guard guard(s_moduleLock);
    if (modules.size() != s_modules.size())
        spdlog::info("scene sync: {} scene plugin(s) loaded", modules.size());
    s_modules = std::move(modules);
}

struct CapturedEvent
{
    uint32_t FormId;
    String Event;
};

std::mutex s_captureLock;
Vector<CapturedEvent> s_captured;

// Last placement forwarded per actor: the plugin re-asserts the same spot every update while the actor is
// not there, which for another owner's actor is always, so only a new spot is forwarded.
Map<uint32_t, String> s_lastPlacement;

using TNotify = bool (*)(IAnimationGraphManagerHolder* apThis, BSFixedString* apEvent);
TNotify s_realPlayerNotify = nullptr;
TNotify s_realCharacterNotify = nullptr;
ptrdiff_t s_holderOffset = 0;

void OnNotified(IAnimationGraphManagerHolder* apThis, BSFixedString* apEvent, const void* apReturnAddress)
{
    if (!apEvent || !apEvent->data || !SceneSyncService::IsSceneCaller(apReturnAddress))
        return;

    const auto* pActor = reinterpret_cast<const Actor*>(reinterpret_cast<const uint8_t*>(apThis) - s_holderOffset);
    SceneSyncService::CaptureEvent(pActor->formID, apEvent->data);
}

bool HookPlayerNotify(IAnimationGraphManagerHolder* apThis, BSFixedString* apEvent)
{
    const bool cResult = s_realPlayerNotify(apThis, apEvent);
    if (cResult)
        OnNotified(apThis, apEvent, _ReturnAddress());
    return cResult;
}

bool HookCharacterNotify(IAnimationGraphManagerHolder* apThis, BSFixedString* apEvent)
{
    const bool cResult = s_realCharacterNotify(apThis, apEvent);
    if (cResult)
        OnNotified(apThis, apEvent, _ReturnAddress());
    return cResult;
}

bool SwapNotify(Actor* apActor, void* apHook, TNotify& aOriginal) noexcept
{
    auto** pVTable = *reinterpret_cast<void***>(&apActor->animationGraphHolder);
    constexpr size_t kNotifySlot = 1;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&pVTable[kNotifySlot], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    aOriginal = reinterpret_cast<TNotify>(pVTable[kNotifySlot]);
    pVTable[kNotifySlot] = apHook;
    VirtualProtect(&pVTable[kNotifySlot], sizeof(void*), oldProtect, &oldProtect);
    return true;
}
} // namespace

SceneSyncService::SceneSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&SceneSyncService::OnUpdate>(this);
}

bool SceneSyncService::IsSceneCaller(const void* apReturnAddress) noexcept
{
    const auto cAddress = reinterpret_cast<uintptr_t>(apReturnAddress);
    std::lock_guard guard(s_moduleLock);
    for (const auto& range : s_modules)
    {
        if (cAddress >= range.Begin && cAddress < range.End)
            return true;
    }
    return false;
}

void SceneSyncService::CaptureEvent(uint32_t aFormId, const char* acpEvent) noexcept
{
    std::lock_guard guard(s_captureLock);
    s_captured.push_back({aFormId, acpEvent});
}

void SceneSyncService::CapturePlacement(uint32_t aFormId, const NiPoint3& acPosition, float aRotationZ) noexcept
{
    // Rounded so the same spot always yields the same text.
    String spec = fmt::format("{}{:.0f}:{:.0f}:{:.0f}:{:.2f}", kPlacementPrefix, acPosition.x, acPosition.y, acPosition.z, aRotationZ).c_str();

    std::lock_guard guard(s_captureLock);
    auto& last = s_lastPlacement[aFormId];
    if (last == spec)
        return;
    last = spec;
    s_captured.push_back({aFormId, std::move(spec)});
}

void SceneSyncService::ApplyPlacement(Actor* apActor, const char* acpSpec) noexcept
{
    float x = 0.f, y = 0.f, z = 0.f, rotation = 0.f;
    if (sscanf_s(acpSpec, "%f:%f:%f:%f", &x, &y, &z, &rotation) != 4)
        return;

    apActor->SetRotation(apActor->rotation.x, apActor->rotation.y, rotation);
    apActor->SetPosition(NiPoint3(glm::vec3(x, y, z)), true);
    spdlog::info("scene sync: placed {:X} for a scene started by another player", apActor->formID);
}

void SceneSyncService::QueueRetry(uint32_t aFormId, const char* acpEvent) noexcept
{
    // The plugin itself retries a refused event every 0.1 s; give the copy a second to reach a state that takes it.
    m_retries.push_back({aFormId, acpEvent, 1.0, 0.1});
}

void SceneSyncService::InstallHooks() noexcept
{
    if (!m_playerHooked)
    {
        if (auto* pPlayer = PlayerCharacter::Get())
        {
            s_holderOffset = reinterpret_cast<uint8_t*>(&pPlayer->animationGraphHolder) - reinterpret_cast<uint8_t*>(static_cast<Actor*>(pPlayer));
            m_playerHooked = SwapNotify(pPlayer, reinterpret_cast<void*>(&HookPlayerNotify), s_realPlayerNotify);
            if (m_playerHooked)
                spdlog::info("scene sync: watching the player's graph events");
        }
    }

    if (!m_characterHooked && m_playerHooked)
    {
        // Every NPC and every other player's copy shares the Character vtable; any loaded one gives it.
        auto view = m_world.view<FormIdComponent>();
        for (auto entity : view)
        {
            auto* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
            if (!pActor || pActor->formType != FormType::Character)
                continue;

            m_characterHooked = SwapNotify(pActor, reinterpret_cast<void*>(&HookCharacterNotify), s_realCharacterNotify);
            if (m_characterHooked)
                spdlog::info("scene sync: watching the graph events of every other character");
            break;
        }
    }
}

void SceneSyncService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    ScanModules();

    if (!m_playerHooked || !m_characterHooked)
        InstallHooks();

    SendCaptured();
    RunRetries(acEvent.Delta);
}

void SceneSyncService::SendCaptured() noexcept
{
    Vector<CapturedEvent> captured;
    {
        std::lock_guard guard(s_captureLock);
        if (s_captured.empty())
            return;
        captured.swap(s_captured);
    }

    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();
    for (const auto& event : captured)
    {
        const auto it = std::find_if(view.begin(), view.end(), [view, id = event.FormId](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });
        if (it == view.end())
        {
            spdlog::debug("scene sync: {:X} is not synced, '{}' stays local", event.FormId, event.Event.c_str());
            continue;
        }

        const auto serverId = Utils::GetServerId(*it);
        if (!serverId)
            continue;

        ScriptAnimationRequest request{};
        request.FormID = *serverId;
        request.EventName = event.Event;
        m_transport.Send(request);

        spdlog::info("scene sync: relayed '{}' for {:X}", event.Event.c_str(), event.FormId);
    }
}

void SceneSyncService::RunRetries(double aDelta) noexcept
{
    for (auto it = m_retries.begin(); it != m_retries.end();)
    {
        it->Remaining -= aDelta;
        it->Wait -= aDelta;
        if (it->Wait > 0.0 && it->Remaining > 0.0)
        {
            ++it;
            continue;
        }

        bool done = it->Remaining <= 0.0;
        if (!done)
        {
            it->Wait = 0.1;
            if (auto* pActor = Cast<Actor>(TESForm::GetById(it->FormId)))
            {
                BSFixedString event(it->Event.c_str());
                done = pActor->SendAnimationEvent(&event);
                event.Release();
                if (done)
                    spdlog::info("scene sync: {:X} took '{}' on a retry", it->FormId, it->Event.c_str());
            }
            else
            {
                done = true;
            }
        }

        if (done && it->Remaining <= 0.0)
            spdlog::info("scene sync: {:X} kept refusing '{}'", it->FormId, it->Event.c_str());

        it = done ? m_retries.erase(it) : it + 1;
    }
}
