#include <Services/RaceMenuSyncService.h>

#include <Components.h>
#include <World.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/NotifyRaceMenuPreset.h>
#include <Messages/RaceMenuPresetRequest.h>

#include <Services/PapyrusService.h>
#include <Services/TransportService.h>
#include <Systems/FaceGenSystem.h>

#include <Actor.h>
#include <PlayerCharacter.h>
#include <Forms/TESNPC.h>
#include <Games/ActorExtension.h>
#include <Interface/UI.h>
#include <Misc/BSFixedString.h>
#include <Misc/TintMask.h>

#include <zlib.h>

#include <filesystem>
#include <fstream>

namespace
{
// RaceMenu reads and writes presets here, relative to the game folder (Mod Organizer redirects new files
// into its overwrite folder). The files only exist for the duration of one save or one load.
constexpr const char* kPresetFolder = "Data\\SKSE\\Plugins\\CharGen\\Presets\\";
constexpr const char* kLocalPresetName = "STRSync_Local";

// RaceMenu's ApplyTypes: face, overrides, body morphs, transforms and skin overrides.
constexpr uint32_t kPresetApplyAll = 0x0F;

// Larger compressed presets than this are not sent; the server enforces the same limit.
constexpr size_t kMaxCompressedBytes = 384 * 1024;

// Give the character a moment to be assigned before the first export after joining.
constexpr double kJoinDelaySeconds = 5.0;
constexpr double kApplyIntervalSeconds = 0.5;
constexpr uint32_t kMaxApplyAttempts = 5;

// The CharGen natives are RaceMenu's SKSE callbacks, which take (StaticFunctionTag*, arguments...). A
// BSFixedString argument is passed by value, which on x64 means a pointer to a temporary that the callee
// releases: the string handed over below carries its own reference for that.
using TSaveCharacterPreset = void (*)(void* apTag, Actor* apActor, BSFixedString* apName);
using TLoadCharacterPresetEx = bool (*)(void* apTag, Actor* apActor, BSFixedString* apName, void* apHairColor, uint32_t aFlags);

std::filesystem::path PresetPath(const std::string& acName)
{
    return std::filesystem::path(kPresetFolder) / (acName + ".jslot");
}

bool ReadFile(const std::filesystem::path& acPath, std::string& aOut)
{
    std::ifstream file(acPath, std::ios::binary);
    if (!file)
        return false;
    aOut.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !aOut.empty();
}

bool WriteFile(const std::filesystem::path& acPath, const String& acData)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(acData.data(), static_cast<std::streamsize>(acData.size()));
    return static_cast<bool>(file);
}

void RemoveFile(const std::filesystem::path& acPath)
{
    std::error_code ec;
    std::filesystem::remove(acPath, ec);
}

bool Compress(const std::string& acInput, String& aOut)
{
    uLongf size = compressBound(static_cast<uLong>(acInput.size()));
    aOut.resize(size);
    if (compress2(reinterpret_cast<Bytef*>(aOut.data()), &size, reinterpret_cast<const Bytef*>(acInput.data()), static_cast<uLong>(acInput.size()),
                  Z_BEST_COMPRESSION) != Z_OK)
        return false;
    aOut.resize(size);
    return true;
}

bool Decompress(const String& acInput, uint32_t aSize, String& aOut)
{
    // A preset is JSON text; refuse anything that claims to be implausibly large.
    if (aSize == 0 || aSize > 16 * 1024 * 1024)
        return false;
    aOut.resize(aSize);
    uLongf size = aSize;
    if (uncompress(reinterpret_cast<Bytef*>(aOut.data()), &size, reinterpret_cast<const Bytef*>(acInput.data()), static_cast<uLong>(acInput.size())) != Z_OK)
        return false;
    aOut.resize(size);
    return true;
}

Tints CaptureLocalTints()
{
    Tints tints;
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return tints;

    const auto& gameTints = pPlayer->GetTints();
    tints.Entries.resize(gameTints.length);
    for (auto i = 0u; i < gameTints.length; ++i)
    {
        tints.Entries[i].Alpha = gameTints[i]->alpha;
        tints.Entries[i].Color = gameTints[i]->color;
        tints.Entries[i].Type = gameTints[i]->type;
        if (gameTints[i]->texture)
            tints.Entries[i].Name = gameTints[i]->texture->name.AsAscii();
    }
    return tints;
}
} // namespace

RaceMenuSyncService::RaceMenuSyncService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&RaceMenuSyncService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&RaceMenuSyncService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&RaceMenuSyncService::OnDisconnected>(this);
    m_notifyConnection = aDispatcher.sink<NotifyRaceMenuPreset>().connect<&RaceMenuSyncService::OnNotifyPreset>(this);
}

bool RaceMenuSyncService::RaceMenuAvailable() noexcept
{
    auto& papyrus = m_world.ctx().at<PapyrusService>();
    const bool cAvailable = papyrus.Get("CharGen", "SaveCharacterPreset") && papyrus.Get("CharGen", "LoadCharacterPresetEx");
    if (!cAvailable && !m_reportedMissing)
    {
        m_reportedMissing = true;
        spdlog::info("RaceMenu sync is off: RaceMenu's CharGen.SaveCharacterPreset/LoadCharacterPresetEx are not registered");
    }
    return cAvailable;
}

void RaceMenuSyncService::OnConnected(const ConnectedEvent&) noexcept
{
    m_sendAfterJoin = true;
    m_joinDelay = 0.0;
}

void RaceMenuSyncService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_remotePresets.clear();
    m_sendAfterJoin = false;
}

void RaceMenuSyncService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    // The owner's side: RaceMenu closing is when sculpting, an imported head or a loaded preset is final.
    static BSFixedString s_raceMenuName("RaceSex Menu");
    const UI* pUI = UI::Get();
    const bool cRaceMenuOpen = pUI && pUI->GetMenuOpen(s_raceMenuName);
    if (m_raceMenuWasOpen && !cRaceMenuOpen)
        SendLocalPreset("RaceMenu closed");
    m_raceMenuWasOpen = cRaceMenuOpen;

    if (m_sendAfterJoin)
    {
        m_joinDelay += acEvent.Delta;
        if (m_joinDelay >= kJoinDelaySeconds && !cRaceMenuOpen)
        {
            m_sendAfterJoin = false;
            SendLocalPreset("joined");
        }
    }

    // The other players' side, a few times a second rather than every frame.
    m_applyTimer += acEvent.Delta;
    if (m_applyTimer >= kApplyIntervalSeconds)
    {
        m_applyTimer = 0.0;
        ApplyPending();
    }
}

void RaceMenuSyncService::SendLocalPreset(const char* acpReason) noexcept
{
    if (!RaceMenuAvailable())
        return;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const TSaveCharacterPreset cSave = reinterpret_cast<TSaveCharacterPreset>(const_cast<void*>(m_world.ctx().at<PapyrusService>().Get("CharGen", "SaveCharacterPreset")));

    const auto cPath = PresetPath(kLocalPresetName);
    RemoveFile(cPath);

    BSFixedString name(kLocalPresetName); // released by the callee
    cSave(nullptr, pPlayer, &name);

    std::string json;
    const bool cRead = ReadFile(cPath, json);
    RemoveFile(cPath);
    if (!cRead)
    {
        spdlog::warn("RaceMenu sync: RaceMenu did not write the player's preset ({})", acpReason);
        return;
    }

    RaceMenuPresetRequest request;
    if (!Compress(json, request.Preset) || request.Preset.size() > kMaxCompressedBytes)
    {
        spdlog::warn("RaceMenu sync: the player's preset is too large to send ({} bytes, {} compressed)", json.size(), request.Preset.size());
        return;
    }

    request.Revision = ++m_localRevision;
    request.UncompressedSize = static_cast<uint32_t>(json.size());
    request.FaceTints = CaptureLocalTints();

    spdlog::info("RaceMenu sync: sending the player's appearance ({}), revision {}, {} bytes ({} compressed)", acpReason, request.Revision, json.size(),
                 request.Preset.size());
    m_transport.Send(request);
}

void RaceMenuSyncService::OnNotifyPreset(const NotifyRaceMenuPreset& acMessage) noexcept
{
    auto& preset = m_remotePresets[acMessage.PlayerId];

    String json;
    if (!Decompress(acMessage.Preset, acMessage.UncompressedSize, json))
    {
        spdlog::warn("RaceMenu sync: could not unpack the appearance of player {:X}", acMessage.PlayerId);
        return;
    }

    preset.Revision = acMessage.Revision;
    preset.Json = std::move(json);
    preset.FaceTints = acMessage.FaceTints;
    preset.FailedAttempts = 0;

    spdlog::info("RaceMenu sync: received the appearance of player {:X}, revision {}, {} bytes", acMessage.PlayerId, acMessage.Revision, preset.Json.size());
}

void RaceMenuSyncService::ApplyPending() noexcept
{
    if (m_remotePresets.empty() || !RaceMenuAvailable())
        return;

    auto view = m_world.view<PlayerComponent, FormIdComponent>();
    for (auto entity : view)
    {
        const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
        auto it = m_remotePresets.find(cPlayerId);
        if (it == m_remotePresets.end())
            continue;

        auto& preset = it.value(); // hopscotch_map: second is read-only
        if (preset.FailedAttempts >= kMaxApplyAttempts)
            continue;

        auto* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        if (!pActor || !pActor->GetExtension()->IsRemotePlayer())
            continue;

        // Wait for the body: RaceMenu rebuilds the head and morphs on the loaded 3D.
        const void* pRoot = pActor->GetNiNode();
        if (!pRoot)
            continue;

        if (preset.AppliedRevision == preset.Revision && preset.AppliedFormId == pActor->formID && preset.AppliedRoot == pRoot)
            continue;

        if (ApplyPreset(cPlayerId, pActor, preset))
        {
            preset.AppliedRevision = preset.Revision;
            preset.AppliedFormId = pActor->formID;
            preset.AppliedRoot = pRoot;

            // Rebuilding the head drops the complexion STR painted on it; repaint it with the owner's tints.
            if (!preset.FaceTints.Entries.empty())
            {
                FaceGenSystem::Setup(m_world, entity, preset.FaceTints);
            }
        }
        else if (++preset.FailedAttempts == kMaxApplyAttempts)
        {
            spdlog::warn("RaceMenu sync: giving up on the appearance of player {:X} after {} attempts", cPlayerId, kMaxApplyAttempts);
        }
    }
}

bool RaceMenuSyncService::ApplyPreset(uint32_t aPlayerId, Actor* apActor, RemotePreset& aPreset) noexcept
{
    const TLoadCharacterPresetEx cLoad =
        reinterpret_cast<TLoadCharacterPresetEx>(const_cast<void*>(m_world.ctx().at<PapyrusService>().Get("CharGen", "LoadCharacterPresetEx")));

    const std::string cName = fmt::format("STRSync_{:X}", aPlayerId);
    const auto cPath = PresetPath(cName);
    if (!WriteFile(cPath, aPreset.Json))
    {
        spdlog::warn("RaceMenu sync: could not write {} for RaceMenu to load", cPath.string());
        return false;
    }

    BSFixedString name(cName.c_str()); // released by the callee
    const bool cLoaded = cLoad(nullptr, apActor, &name, nullptr, kPresetApplyAll);
    RemoveFile(cPath);

    spdlog::info("RaceMenu sync: appearance revision {} of player {:X} {} on {:X}", aPreset.Revision, aPlayerId, cLoaded ? "applied" : "was refused by RaceMenu",
                 apActor->formID);
    return cLoaded;
}
