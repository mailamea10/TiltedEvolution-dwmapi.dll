#include <Services/LipSyncTogetherService.h>

#include <LipSyncTogether/LipSyncNatives.h>
#include <LipSyncTogether/STRLipSyncAPI.h>

#include <Components.h>
#include <World.h>

#include <Events/UpdateEvent.h>

#include <Messages/NotifyScriptAnimation.h>
#include <Messages/ScriptAnimationRequest.h>

#include <Services/ImguiService.h>
#include <Services/OverlayService.h>
#include <Services/TransportService.h>

#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Interface/UI.h>
#include <Misc/BSFixedString.h>
#include <Misc/NativeFunction.h>

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>

// Lets the plugin see that this Skyrim Together build carries the add-on.
extern "C" __declspec(dllexport) uint32_t STRLipSync_HostApiVersion()
{
    return STRLipSync::kApiVersion;
}

namespace
{
// "STRLS1|<sender's player id, hex>|<mouth state>", relayed as a script animation for an object id that the stock
// client drops without playing anything. 1.8.5 resolves that id as a server id, so one that cannot exist is used;
// 1.8.3 skips id 0 outright (apply_addon.py --protocol 1.8.3 defines it).
#ifndef STR_LIPSYNC_NO_OBJECT
#define STR_LIPSYNC_NO_OBJECT 0xFFFFFFFFu
#endif
constexpr const char* kWirePrefix = "STRLS1|";
constexpr const char* kVoiceKeyPrefix = "STRVK1|";
constexpr uint32_t kNoObject = STR_LIPSYNC_NO_OBJECT;

// A mouth state is three characters: the jaw (0-7), the strongest lip shape (N none, W wide, R round, L lips
// closed, T teeth) and its strength (0 for N, else 1-3). "-" means the speaker went quiet. The server keeps every
// distinct relayed string until it restarts and hands the whole set to everyone who joins, so the states are a
// fixed set of 104 per player rather than raw numbers.
constexpr const char* kQuietState = "-";
constexpr int kJawLevels = 7;
constexpr int kShapeLevels = 3;

// At most 30 states a second, an unchanged state at most every 0.2 s to show the speaker is still talking, and a
// copy that hears nothing for 0.6 s closes its mouth.
constexpr double kMinSendInterval = 1.0 / 30.0;
constexpr double kRepeatInterval = 0.2;
constexpr double kRemoteTimeout = 0.6;
constexpr size_t kMaxQueued = 32;

// Faces are written at most this often.
constexpr double kApplyInterval = 1.0 / 30.0;

// The plugin can load late, so keep looking for it for a while.
constexpr double kScanInterval = 5.0;
constexpr double kScanDuration = 600.0;

// Mfg Fix registers its natives while the game starts; give it this long before reporting it missing.
constexpr double kFaceFunctionGrace = 20.0;

// Mfg Fix's SetPhonemeModifier modes and Skyrim's phoneme ids.
constexpr int32_t kModePhoneme = 0;
enum Phoneme : uint32_t
{
    kAah = 0,
    kBigAah = 1,
    kBMP = 2,
    kChJSh = 3,
    kDST = 4,
    kEee = 5,
    kEh = 6,
    kFV = 7,
    kI = 8,
    kOh = 11,
    kOohQ = 12,
    kPhonemeCount = 16,
};

using Shape = LipSyncTogetherService::Shape;

std::atomic<NativeFunction*> s_setPhonemeModifier{nullptr};

// The server the game last dialled ("host:port"), from the connect handler.
std::mutex s_endpointLock;
std::string s_endpoint;

// "host:port" or "[v6]:port" -> host.
std::string HostOf(const std::string& acEndpoint)
{
    if (!acEndpoint.empty() && acEndpoint.front() == '[')
    {
        const size_t cClose = acEndpoint.find(']');
        return cClose == std::string::npos ? std::string{} : acEndpoint.substr(1, cClose - 1);
    }
    const size_t cColon = acEndpoint.rfind(':');
    // More than one colon without brackets is a bare IPv6 address.
    if (cColon == std::string::npos || acEndpoint.find(':') != cColon)
        return acEndpoint;
    return acEndpoint.substr(0, cColon);
}

bool StartsWith(const String& acText, const char* acpPrefix) noexcept
{
    const size_t cLength = strlen(acpPrefix);
    return acText.size() >= cLength && memcmp(acText.data(), acpPrefix, cLength) == 0;
}

float Clamp01(float aValue) noexcept
{
    return std::clamp(aValue, 0.f, 1.f);
}

// --- memory checks for resolving Mfg Fix's native ------------------------------------------------------------

bool IsReadable(const void* apAddress, size_t aSize) noexcept
{
    MEMORY_BASIC_INFORMATION info{};
    if (!apAddress || !VirtualQuery(apAddress, &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return false;
    const auto cEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return reinterpret_cast<uintptr_t>(apAddress) + aSize <= cEnd;
}

bool IsExecutable(const void* apAddress) noexcept
{
    MEMORY_BASIC_INFORMATION info{};
    if (!apAddress || !VirtualQuery(apAddress, &info, sizeof(info)) || info.State != MEM_COMMIT)
        return false;
    return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// The decorated class name MSVC's RTTI gives a vtable, or nullptr when it has none.
const char* VtableClassName(const void* apVtable) noexcept
{
    struct CompleteObjectLocator
    {
        uint32_t Signature; // 1 on x64
        uint32_t Offset;
        uint32_t ConstructorOffset;
        int32_t TypeDescriptor; // image-relative
        int32_t ClassDescriptor;
        int32_t Self;
    };

    const auto* pSlot = reinterpret_cast<const uint8_t*>(apVtable) - sizeof(void*);
    if (!IsReadable(pSlot, sizeof(void*)))
        return nullptr;
    const auto* pLocator = *reinterpret_cast<const CompleteObjectLocator* const*>(pSlot);
    if (!IsReadable(pLocator, sizeof(CompleteObjectLocator)) || pLocator->Signature != 1)
        return nullptr;

    const auto cImage = reinterpret_cast<uintptr_t>(pLocator) - static_cast<uintptr_t>(pLocator->Self);
    const auto* pName = reinterpret_cast<const char*>(cImage + pLocator->TypeDescriptor + 2 * sizeof(void*));
    if (!IsReadable(pName, 8) || pName[0] != '.')
        return nullptr;
    return pName;
}

// Mfg Fix NG (and other CommonLib plugins) register a native whose callback is a std::function at offset 0x50,
// where a plain SKSE native keeps its function pointer. MSVC's std::function stores a function pointer in place:
// the implementation object (vtable, then the pointer) at the start of its 64 bytes, and a pointer to that object
// in its last 8. Both are checked, along with the implementation's RTTI name and signature, before anything is
// called; an unexpected layout leaves lip sync off with a log line rather than crashing.

void* ResolveSetPhonemeModifier(NativeFunction* apFunction, String& aHow) noexcept
{
    auto* pObject = reinterpret_cast<uint8_t*>(apFunction);
    constexpr size_t kCallbackOffset = 0x50;
    constexpr size_t kFunctionSize = 0x40;
    if (!IsReadable(pObject + kCallbackOffset, kFunctionSize))
    {
        aHow = "its function object cannot be read";
        return nullptr;
    }

    void* pFirst = *reinterpret_cast<void**>(pObject + kCallbackOffset);
    if (IsExecutable(pFirst))
    {
        aHow = "plain SKSE native";
        return pFirst;
    }

    const char* pName = VtableClassName(pFirst);
    // std::_Func_impl_no_alloc<bool (*)(RE::StaticFunctionTag*, RE::Actor*, int, unsigned int, int), ...>
    constexpr const char* kSignature = "P6A_NPEAUStaticFunctionTag@RE@@PEAVActor@2@HIH@Z";
    if (!pName || !strstr(pName, "_Func_impl") || !strstr(pName, kSignature))
    {
        aHow = fmt::format("its callback is not a function pointer kept in a std::function ({})", pName ? pName : "no RTTI").c_str();
        return nullptr;
    }

    void* pImpl = *reinterpret_cast<void**>(pObject + kCallbackOffset + kFunctionSize - sizeof(void*));
    if (pImpl != pObject + kCallbackOffset)
    {
        aHow = "its std::function does not keep the callback in place";
        return nullptr;
    }

    void* pCallee = *reinterpret_cast<void**>(pObject + kCallbackOffset + sizeof(void*));
    if (!IsExecutable(pCallee))
    {
        aHow = "the function pointer in its std::function is not code";
        return nullptr;
    }

    aHow = "CommonLib native";
    return pCallee;
}

// --- mouth states ----------------------------------------------------------------------------------------------

String EncodeState(const Shape& acMouth) noexcept
{
    const int cJaw = std::clamp(static_cast<int>(std::lround(acMouth.Open * kJawLevels)), 0, kJawLevels);

    const std::pair<char, float> cShapes[] = {{'W', acMouth.Wide}, {'R', acMouth.Round}, {'L', acMouth.Lips}, {'T', acMouth.Teeth}};
    auto strongest = std::max_element(std::begin(cShapes), std::end(cShapes), [](const auto& a, const auto& b) { return a.second < b.second; });

    String state;
    state.push_back(static_cast<char>('0' + cJaw));
    if (strongest->second < 0.2f)
    {
        state += "N0";
        return state;
    }
    state.push_back(strongest->first);
    state.push_back(static_cast<char>('0' + std::clamp(static_cast<int>(std::lround(strongest->second * kShapeLevels)), 1, kShapeLevels)));
    return state;
}

bool DecodeState(const String& acState, Shape& aOut) noexcept
{
    aOut = Shape{};
    if (acState == kQuietState)
        return true;
    if (acState.size() != 3 || acState[0] < '0' || acState[0] > '0' + kJawLevels || acState[2] < '0' || acState[2] > '0' + kShapeLevels)
        return false;

    aOut.Open = static_cast<float>(acState[0] - '0') / kJawLevels;
    const float cStrength = static_cast<float>(acState[2] - '0') / kShapeLevels;
    switch (acState[1])
    {
    case 'N': break;
    case 'W': aOut.Wide = cStrength; break;
    case 'R': aOut.Round = cStrength; break;
    case 'L': aOut.Lips = cStrength; break;
    case 'T': aOut.Teeth = cStrength; break;
    default: return false;
    }
    return true;
}

// The five shapes as Skyrim phonemes, 0 to 100. The weights stay near what vanilla dialogue uses: ordinary speech
// is a modest Aah and Eh, spread and rounded lips only add some Eee/I or OohQ/Oh on top, and BigAah is for shouting.
void ToPhonemes(const Shape& acMouth, float aStrength, int (&aOut)[kPhonemeCount]) noexcept
{
    float weight[kPhonemeCount]{};
    const float cOpen = acMouth.Open * (1.f - 0.6f * acMouth.Lips);
    weight[kAah] = 0.55f * cOpen * (1.f - 0.5f * acMouth.Round) * (1.f - 0.4f * acMouth.Wide);
    weight[kBigAah] = 0.3f * Clamp01((acMouth.Open - 0.85f) / 0.15f);
    weight[kEh] = 0.3f * cOpen * (1.f - acMouth.Round);
    weight[kEee] = 0.4f * acMouth.Wide;
    weight[kI] = 0.25f * acMouth.Wide * acMouth.Open;
    weight[kOohQ] = 0.45f * acMouth.Round;
    weight[kOh] = 0.3f * acMouth.Round * acMouth.Open;
    weight[kBMP] = 0.5f * acMouth.Lips;
    weight[kFV] = 0.35f * acMouth.Teeth;
    weight[kDST] = 0.25f * acMouth.Teeth;
    weight[kChJSh] = 0.15f * acMouth.Teeth;

    for (uint32_t i = 0; i < kPhonemeCount; ++i)
        aOut[i] = std::clamp(static_cast<int>(std::lround(weight[i] * aStrength * 100.f)), 0, 100);
}

// Moves aCurrent toward aTarget, opening faster than closing. The sender has already smoothed the voice, so this
// only rounds off the steps between received states.
void SmoothToward(Shape& aCurrent, const Shape& acTarget, double aDelta) noexcept
{
    const float cAttack = 1.f - std::exp(static_cast<float>(-aDelta / 0.015));
    const float cRelease = 1.f - std::exp(static_cast<float>(-aDelta / 0.035));
    auto step = [&](float& aValue, float aGoal) {
        aValue += (aGoal - aValue) * (aGoal > aValue ? cAttack : cRelease);
        if (aValue < 0.005f)
            aValue = 0.f;
    };
    step(aCurrent.Open, acTarget.Open);
    step(aCurrent.Wide, acTarget.Wide);
    step(aCurrent.Round, acTarget.Round);
    step(aCurrent.Lips, acTarget.Lips);
    step(aCurrent.Teeth, acTarget.Teeth);
}

const STRLipSync::Descriptor* Descriptor(const void* apDescriptor) noexcept
{
    return static_cast<const STRLipSync::Descriptor*>(apDescriptor);
}
} // namespace

void LipSyncTogether_OnNativeRegistered(NativeFunction* apFunction) noexcept
{
    if (!apFunction || s_setPhonemeModifier.load())
        return;
    const char* pType = apFunction->typeName.AsAscii();
    const char* pName = apFunction->functionName.AsAscii();
    if (pType && pName && !_stricmp(pType, "MfgConsoleFunc") && !_stricmp(pName, "SetPhonemeModifier"))
        s_setPhonemeModifier.store(apFunction);
}

void LipSyncTogether_OnConnect(const char* acpEndpoint) noexcept
{
    std::lock_guard guard(s_endpointLock);
    s_endpoint = acpEndpoint ? acpEndpoint : "";
}

void LipSyncTogetherService::FaceState::Forget() noexcept
{
    std::fill(std::begin(Written), std::end(Written), static_cast<int8_t>(-1));
    Owned = false;
}

LipSyncTogetherService::LipSyncTogetherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_window(aWorld.ctx().at<ImguiService>())
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&LipSyncTogetherService::OnUpdate>(this);
    m_notifyConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&LipSyncTogetherService::OnNotifyScriptAnimation>(this);
}

void LipSyncTogetherService::ScanForPlugin() noexcept
{
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return;

    const size_t cCount = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));
    for (size_t i = 0; i < cCount; ++i)
    {
        const auto getDescriptor = reinterpret_cast<STRLipSync::TGetDescriptor>(GetProcAddress(modules[i], STRLipSync::kDescriptorExport));
        if (!getDescriptor)
            continue;

        char path[MAX_PATH]{};
        K32GetModuleBaseNameA(GetCurrentProcess(), modules[i], path, MAX_PATH);

        const STRLipSync::Descriptor* pDescriptor = getDescriptor();
        if (!pDescriptor || pDescriptor->ApiVersion != STRLipSync::kApiVersion || !pDescriptor->GetFrame || !pDescriptor->Config ||
            !pDescriptor->GetSettingCount || !pDescriptor->GetSettingInfo || !pDescriptor->GetSetting || !pDescriptor->SetSetting ||
            !pDescriptor->SaveSettings || !pDescriptor->ReloadSettings || !pDescriptor->HasUnsavedChanges || !pDescriptor->ListDevices ||
            !pDescriptor->GetMeter || !pDescriptor->GetVoiceStatus || !pDescriptor->GetVoiceKey || !pDescriptor->AddVoiceKey)
        {
            spdlog::warn("LipSync Together: {} has a descriptor this add-on cannot use (API {}, add-on API {})", path, pDescriptor ? pDescriptor->ApiVersion : 0,
                         STRLipSync::kApiVersion);
            continue;
        }

        m_pDescriptor = pDescriptor;
        for (auto it = m_pendingVoiceKeys.begin(); it != m_pendingVoiceKeys.end(); ++it)
            m_pDescriptor->AddVoiceKey(it->first, it->second.c_str());
        m_pendingVoiceKeys.clear();
        m_window.SetDescriptor(pDescriptor);
        spdlog::info("LipSync Together: found {} {} in {} (own face {}, strength {:.2f}, remote delay {:.0f} ms, max distance {:.0f})",
                     pDescriptor->ModName ? pDescriptor->ModName : "?", pDescriptor->ModVersion ? pDescriptor->ModVersion : "", path,
                     pDescriptor->Config->AnimateSelf ? "on" : "off", pDescriptor->Config->MouthStrength, pDescriptor->Config->RemoteDelaySeconds * 1000.f,
                     pDescriptor->Config->MaxDistance);
        return;
    }
}

bool LipSyncTogetherService::ResolveFaceFunction() noexcept
{
    if (m_setPhonemeModifier)
        return true;
    if (m_faceFunctionChecked)
        return false;

    NativeFunction* pFunction = s_setPhonemeModifier.load();
    if (!pFunction)
    {
        if (m_faceFunctionWait < kFaceFunctionGrace)
            return false;
        m_faceFunctionChecked = true;
        spdlog::warn("LipSync Together: MfgConsoleFunc.SetPhonemeModifier was never registered. Install Mfg Fix NG (or Mfg Fix on 1.5.97); "
                     "until then nobody's mouth moves on this machine (mouth states are still sent)");
        return false;
    }

    m_faceFunctionChecked = true;
    String how;
    m_setPhonemeModifier = reinterpret_cast<TSetPhonemeModifier>(ResolveSetPhonemeModifier(pFunction, how));
    if (m_setPhonemeModifier)
        spdlog::info("LipSync Together: faces are moved with MfgConsoleFunc.SetPhonemeModifier ({}, {})", how.c_str(), reinterpret_cast<const void*>(m_setPhonemeModifier));
    else
        spdlog::error("LipSync Together: cannot call MfgConsoleFunc.SetPhonemeModifier: {}; mouths are not moved on this machine", how.c_str());
    return m_setPhonemeModifier != nullptr;
}

void LipSyncTogetherService::Reset() noexcept
{
    m_remotes.clear();
    m_lastSentState.clear();
    m_lastSendTime = -1.0;
    m_sentSpeaking = false;
}

void LipSyncTogetherService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_pDescriptor && m_scanElapsed < kScanDuration)
    {
        m_scanElapsed += acEvent.Delta;
        m_scanTimer -= acEvent.Delta;
        if (m_scanTimer <= 0.0)
        {
            m_scanTimer = kScanInterval;
            ScanForPlugin();
        }
    }
    if (!m_pDescriptor)
        return;

    m_clock += acEvent.Delta;
    m_faceFunctionWait += acEvent.Delta;
    ReportTiming(acEvent.Delta);
    ResolveFaceFunction();

    const bool cOnline = m_transport.IsOnline();
    if (cOnline != m_wasOnline)
    {
        m_wasOnline = cOnline;
        Reset();
    }

    m_applyTimer -= acEvent.Delta;
    const bool cApply = m_applyTimer <= 0.0 && m_setPhonemeModifier;
    if (m_applyTimer <= 0.0)
        m_applyTimer = kApplyInterval;

    UpdateWindow();
    UpdateLocal(acEvent.Delta, cApply);
    UpdateVoice(cOnline, cApply);
    if (cOnline)
        UpdateRemotes(acEvent.Delta, cApply);
}

void LipSyncTogetherService::UpdateLocal(double aDelta, bool aApply) noexcept
{
    const auto* pDescriptor = Descriptor(m_pDescriptor);

    STRLipSync::Frame frame{};
    if (!pDescriptor->GetFrame(&frame))
        frame = STRLipSync::Frame{};

    const Shape cMouth{Clamp01(frame.Open), Clamp01(frame.Wide), Clamp01(frame.Round), Clamp01(frame.Lips), Clamp01(frame.Teeth)};
    const bool cSpeaking = frame.Speaking != 0;

    if (m_transport.IsOnline())
        SendLocal(cMouth, cSpeaking);

    if (!aApply)
        return;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    // The player's face is the dialogue's business while a conversation is open.
    static BSFixedString s_dialogueMenu("Dialogue Menu");
    const UI* pUI = UI::Get();
    const bool cInDialogue = pUI && pUI->GetMenuOpen(s_dialogueMenu);

    if (pDescriptor->Config->AnimateSelf && !cInDialogue && pPlayer->GetNiNode() && (cSpeaking || !cMouth.IsClosed()))
        WriteFace(pPlayer, m_localFace, cMouth);
    else
        ReleaseFace(pPlayer, m_localFace);
}

void LipSyncTogetherService::SendLocal(const Shape& acMouth, bool aSpeaking) noexcept
{
    const bool cActive = aSpeaking || !acMouth.IsClosed();
    const double cSince = m_lastSendTime < 0.0 ? 1e9 : m_clock - m_lastSendTime;

    String state;
    if (cActive)
    {
        state = EncodeState(acMouth);
        if (cSince < kMinSendInterval || (state == m_lastSentState && cSince < kRepeatInterval))
            return;
    }
    else
    {
        if (!m_sentSpeaking)
            return;
        state = kQuietState;
    }

    ScriptAnimationRequest request{};
    request.FormID = kNoObject;
    request.EventName = fmt::format("{}{:X}|{}", kWirePrefix, m_transport.GetLocalPlayerId(), state.c_str()).c_str();
    m_transport.Send(request);
    ++m_sentCount;

    if (Descriptor(m_pDescriptor)->Config->Debug && state != m_lastSentState)
        spdlog::info("LipSync Together: sent {}", state.c_str());

    m_lastSentState = state;
    m_lastSendTime = m_clock;
    m_sentSpeaking = cActive;
}

void LipSyncTogetherService::OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept
{
    const String& cWire = acMessage.EventName;

    // The current player's random voice key goes through the existing server string relay. A player hosting the
    // voice relay accepts only the keys it has received this way.
    if (StartsWith(cWire, kVoiceKeyPrefix))
    {
        const size_t cStart = strlen(kVoiceKeyPrefix);
        const size_t cBar = cWire.find('|', cStart);
        if (cBar == String::npos || cBar == cStart || cBar - cStart > 8)
            return;
        const uint32_t cPlayerId = static_cast<uint32_t>(strtoul(cWire.substr(cStart, cBar - cStart).c_str(), nullptr, 16));
        const String cKey = cWire.substr(cBar + 1);
        if (cKey.size() != 32)
            return;
        for (const char c : cKey)
        {
            if (!isxdigit(static_cast<unsigned char>(c)))
                return;
        }
        if (m_pDescriptor)
            m_pDescriptor->AddVoiceKey(cPlayerId, cKey.c_str());
        else if (m_pendingVoiceKeys.size() < 256)
            m_pendingVoiceKeys[cPlayerId] = cKey;



        return;
    }

    if (!m_pDescriptor || !StartsWith(cWire, kWirePrefix))
        return;

    const size_t cStart = strlen(kWirePrefix);
    const size_t cBar = cWire.find('|', cStart);
    if (cBar == String::npos || cBar == cStart || cBar - cStart > 8)
        return;

    const uint32_t cPlayerId = static_cast<uint32_t>(strtoul(cWire.substr(cStart, cBar - cStart).c_str(), nullptr, 16));
    // 1.8.3 servers send our own states back to us.
    if (cPlayerId == m_transport.GetLocalPlayerId())
        return;

    Shape mouth;
    if (!DecodeState(cWire.substr(cBar + 1), mouth))
        return;

    auto& remote = m_remotes[cPlayerId];
    // Gaps within a stretch of talking; the silence before someone starts is not a delay.
    if (remote.LastReceived > 0.0 && m_clock - remote.LastReceived < 1.0)
        remote.LargestGap = std::max(remote.LargestGap, m_clock - remote.LastReceived);
    ++remote.ReceivedCount;

    if (Descriptor(m_pDescriptor)->Config->Debug)
    {
        const String cState = cWire.substr(cBar + 1);
        if (cState != remote.LastState)
            spdlog::info("LipSync Together: received {} from player {:X}", cState.c_str(), cPlayerId);
        remote.LastState = cState;
    }

    if (remote.Queue.size() >= kMaxQueued)
        remote.Queue.erase(remote.Queue.begin());
    remote.Queue.push_back(Received{m_clock, mouth});
    remote.LastReceived = m_clock;
}

void LipSyncTogetherService::UpdateRemotes(double aDelta, bool aApply) noexcept
{
    if (m_remotes.empty())
        return;

    const auto* pDescriptor = Descriptor(m_pDescriptor);
    const double cDelay = pDescriptor->Config->RemoteDelaySeconds;
    const float cMaxDistance = pDescriptor->Config->MaxDistance;

    for (auto it = m_remotes.begin(); it != m_remotes.end(); ++it)
    {
        auto& remote = it.value(); // hopscotch_map: second is read-only

        // A player whose voice is heard: the mouth follows the voice as it plays.
        STRLipSync::Frame voiced{};
        if (m_voiceSession && pDescriptor->GetRemoteMouth && pDescriptor->GetRemoteMouth(it->first, &voiced))
        {
            remote.Queue.clear();
            remote.Target = Shape{Clamp01(voiced.Open), Clamp01(voiced.Wide), Clamp01(voiced.Round), Clamp01(voiced.Lips), Clamp01(voiced.Teeth)};
            remote.Current = remote.Target;
            continue;
        }

        // Show what arrived a moment ago, so uneven arrival does not make the mouth stutter.
        size_t due = 0;
        while (due < remote.Queue.size() && remote.Queue[due].Time + cDelay <= m_clock)
            remote.Target = remote.Queue[due++].Mouth;
        if (due > 0)
            remote.Queue.erase(remote.Queue.begin(), remote.Queue.begin() + due);

        if (m_clock - remote.LastReceived > kRemoteTimeout + cDelay)
        {
            remote.Target = Shape{};
            remote.Queue.clear();
        }

        SmoothToward(remote.Current, remote.Target, aDelta);
    }

    if (!aApply)
        return;

    auto* pPlayer = PlayerCharacter::Get();
    auto view = m_world.view<PlayerComponent, FormIdComponent>();
    for (auto entity : view)
    {
        const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
        auto it = m_remotes.find(cPlayerId);
        if (it == m_remotes.end())
            continue;
        auto& remote = it.value();

        auto* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        if (!pActor || !pActor->GetExtension()->IsRemotePlayer())
        {
            remote.Face.Forget();
            continue;
        }

        bool inRange = pActor->GetNiNode() != nullptr;
        if (inRange && pPlayer)
        {
            const float cDx = pActor->position.x - pPlayer->position.x;
            const float cDy = pActor->position.y - pPlayer->position.y;
            const float cDz = pActor->position.z - pPlayer->position.z;
            inRange = cDx * cDx + cDy * cDy + cDz * cDz <= cMaxDistance * cMaxDistance;
        }

        if (inRange && !remote.Current.IsClosed())
            WriteFace(pActor, remote.Face, remote.Current);
        else
            ReleaseFace(pActor, remote.Face);
    }
}

void LipSyncTogetherService::WriteFace(Actor* apActor, FaceState& aFace, const Shape& acMouth) noexcept
{
    if (!m_setPhonemeModifier || !apActor)
        return;

    // A respawned copy is a new face.
    if (aFace.FormId != apActor->formID)
    {
        aFace.Forget();
        aFace.FormId = apActor->formID;
    }

    int values[kPhonemeCount];
    ToPhonemes(acMouth, Descriptor(m_pDescriptor)->Config->MouthStrength, values);

    for (uint32_t i = 0; i < kPhonemeCount; ++i)
    {
        // Leave alone a phoneme this service never raised and has no reason to now.
        if (aFace.Written[i] == values[i] || (aFace.Written[i] < 0 && values[i] == 0))
            continue;
        m_setPhonemeModifier(nullptr, apActor, kModePhoneme, i, values[i]);
        aFace.Written[i] = static_cast<int8_t>(values[i]);
        aFace.Owned = true;
    }
}

void LipSyncTogetherService::ReleaseFace(Actor* apActor, FaceState& aFace) noexcept
{
    if (!aFace.Owned)
        return;

    // Put back to zero only what this service raised, then forget the face so other mods have it to themselves.
    if (m_setPhonemeModifier && apActor && aFace.FormId == apActor->formID)
    {
        for (uint32_t i = 0; i < kPhonemeCount; ++i)
        {
            if (aFace.Written[i] > 0)
                m_setPhonemeModifier(nullptr, apActor, kModePhoneme, i, 0);
        }
    }
    aFace.Forget();
}

void LipSyncTogetherService::ReportTiming(double aDelta) noexcept
{
    if (!Descriptor(m_pDescriptor)->Config->Debug)
        return;

    m_frameTimeSum += aDelta;
    m_longestFrame = std::max(m_longestFrame, aDelta);
    ++m_frameCount;

    m_reportTimer += aDelta;
    if (m_reportTimer < 1.0)
        return;

    // Each "sent"/"received" line carries the log's own millisecond timestamp, so with both games on one PC the
    // two logs show how long a state takes from one to the other.
    spdlog::info("LipSync Together: last {:.1f} s: {} states sent, frame time {:.1f} ms (longest {:.1f} ms)", m_reportTimer, m_sentCount,
                 m_frameCount ? m_frameTimeSum / m_frameCount * 1000.0 : 0.0, m_longestFrame * 1000.0);
    for (auto it = m_remotes.begin(); it != m_remotes.end(); ++it)
    {
        auto& remote = it.value();
        spdlog::info("LipSync Together:   player {:X}: {} states received, longest gap {:.0f} ms, {} waiting, jaw {:.2f}", it->first, remote.ReceivedCount,
                     remote.LargestGap * 1000.0, remote.Queue.size(), remote.Current.Open);
        remote.ReceivedCount = 0;
        remote.LargestGap = 0.0;
    }

    m_reportTimer = 0.0;
    m_sentCount = 0;
    m_frameTimeSum = 0.0;
    m_longestFrame = 0.0;
    m_frameCount = 0;
}

void LipSyncTogetherService::UpdateVoice(bool aOnline, bool aMix) noexcept
{
    const auto* pDescriptor = Descriptor(m_pDescriptor);
    if (!pDescriptor->SetSession || !pDescriptor->SetMix)
        return;
    if (!pDescriptor->Config->VoiceChat)
    {
        // Switched off in the settings window: leave the voice session.
        if (m_voiceSession)
        {
            pDescriptor->SetSession(nullptr, 0);
            m_voiceSession = false;
            m_voiceHost.clear();
            spdlog::info("LipSync Together: voice chat switched off");
        }
        return;
    }

    // The session follows the connection: the server's host, where the relay runs, and our player id.
    std::string host;
    {
        std::lock_guard guard(s_endpointLock);
        host = HostOf(s_endpoint);
    }
    const uint32_t cPlayerId = m_transport.GetLocalPlayerId();
    if (aOnline && !host.empty())
    {
        if (!m_voiceSession || m_voiceHost != host.c_str() || m_voicePlayer != cPlayerId)
        {
            pDescriptor->SetSession(host.c_str(), cPlayerId);
            m_voiceSession = true;
            m_voiceHost = host.c_str();
            m_voicePlayer = cPlayerId;

            // Publish our random voice key once per STR session. The server retains distinct relayed strings and
            // delivers them to later joiners, so the host can authenticate their relay hello packets.
            char voiceKey[33]{};
            if (pDescriptor->GetVoiceKey(voiceKey, sizeof(voiceKey)))
            {
                ScriptAnimationRequest request{};
                request.FormID = kNoObject;
                request.EventName = fmt::format("{}{:X}|{}", kVoiceKeyPrefix, cPlayerId, voiceKey).c_str();
                m_transport.Send(request);
            }

            spdlog::info("LipSync Together: voice chat on the server at {} as player {:X}", host, cPlayerId);
        }
    }
    else if (m_voiceSession)
    {
        pDescriptor->SetSession(nullptr, 0);
        m_voiceSession = false;
        m_voiceHost.clear();
        spdlog::info("LipSync Together: voice chat stopped (left the server)");
    }

    if (!aMix || !m_voiceSession)
        return;

    // Who might be heard and from which side, panned by where they stand relative to the way the player faces. The
    // plugin turns the distance into a volume with the range each voice carries right now (a yell reaches further),
    // so everyone up to the furthest a yell carries is listed.
    const float cReach = std::max(pDescriptor->Config->FarDistance, pDescriptor->Config->NearDistance + 1.f) *
                         std::max(1.f, pDescriptor->Config->MaxRangeScale);
    auto* pPlayer = PlayerCharacter::Get();
    m_mix.clear();
    if (pPlayer)
    {
        auto view = m_world.view<PlayerComponent, FormIdComponent>();
        for (auto entity : view)
        {
            auto* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
            if (!pActor || !pActor->GetExtension()->IsRemotePlayer() || !pActor->GetNiNode())
                continue;

            const float cDx = pActor->position.x - pPlayer->position.x;
            const float cDy = pActor->position.y - pPlayer->position.y;
            const float cDz = pActor->position.z - pPlayer->position.z;
            const float cDistance = std::sqrt(cDx * cDx + cDy * cDy + cDz * cDz);
            if (cDistance >= cReach)
                continue;

            // Skyrim's heading: 0 faces +Y (north), growing clockwise; the angle to the speaker the same way.
            const float cAngle = std::atan2(cDx, cDy) - pPlayer->rotation.z;
            const float cPan = 0.8f * std::sin(cAngle);

            const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
            m_mix.push_back(STRLipSync::MixEntry{cPlayerId, cDistance, cPan});
            // So the voice-driven mouth is applied even before any relayed mouth state arrives.
            m_remotes[cPlayerId];
        }
    }
    pDescriptor->SetMix(m_mix.data(), static_cast<uint32_t>(m_mix.size()));
}

void LipSyncTogetherService::UpdateWindow() noexcept
{
    const auto* pDescriptor = Descriptor(m_pDescriptor);
    auto& overlay = m_world.ctx().at<OverlayService>();

    // The window key (Shift+V by default), only while the game has the focus and the window is not waiting for a
    // key to assign.
    bool toggle = false;
    const uint32_t cOpenKey = pDescriptor->Config->OpenKey;
    if (cOpenKey && !m_window.CapturingKey)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &processId);
        const bool cFocus = processId == GetCurrentProcessId();
        const bool cDown = cFocus && (GetAsyncKeyState(static_cast<int>(cOpenKey)) & 0x8000) != 0;
        const bool cShift = !pDescriptor->Config->OpenKeyShift || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        toggle = cDown && !m_openKeyDown && cShift;
        m_openKeyDown = cDown;
    }

    const bool cVisible = m_window.Visible;
    if (m_window.CloseRequested.exchange(false) || (toggle && cVisible))
    {
        CloseWindow();
        return;
    }
    if (toggle && !cVisible)
    {
        if (!overlay.GetInGame())
            return;
        m_window.RefreshDevices = true;
        m_window.Visible = true;
        // Skyrim Together's overlay shows the cursor and keeps clicks from reaching the game.
        if (!overlay.GetActive())
        {
            overlay.SetActive(true);
            m_windowOpenedOverlay = true;
        }
        spdlog::info("LipSync Together: settings window opened");
        return;
    }
    // Skyrim Together's menu was closed (its own key or Escape): the window goes with it.
    if (cVisible && !overlay.GetActive())
    {
        m_window.Visible = false;
        m_windowOpenedOverlay = false;
    }
}

void LipSyncTogetherService::CloseWindow() noexcept
{
    m_window.Visible = false;
    auto& overlay = m_world.ctx().at<OverlayService>();
    if (m_windowOpenedOverlay && overlay.GetActive())
        overlay.SetActive(false);
    m_windowOpenedOverlay = false;
    spdlog::info("LipSync Together: settings window closed");
}
