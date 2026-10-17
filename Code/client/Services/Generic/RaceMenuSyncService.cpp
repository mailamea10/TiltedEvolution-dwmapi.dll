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
#include <Forms/BGSColorForm.h>
#include <Forms/TESNPC.h>
#include <Games/ActorExtension.h>
#include <Games/IFormFactory.h>
#include <Interface/UI.h>
#include <Misc/BSFixedString.h>
#include <Misc/TintMask.h>

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

// A second full pass this long after a load, once RaceMenu's queued overlay builds and the copy's equipment have
// settled, and how often the copy's body morphs are compared against the owner's.
constexpr double kSettleSeconds = 4.0;
constexpr double kMorphCheckSeconds = 5.0;

// BGSColorForm (CLFM).
constexpr auto kColorFormType = static_cast<FormType>(0x85);

// Morph keys that BodyGen and OBody put on NPCs, which a copy of a player is to them. Body morphs from the
// owner's preset with the same key are kept; anything else under these keys is the copy being regenerated.
constexpr const char* kForeignMorphKeys[] = {"RSMBodyGen", "OBody", "OBodyNG"};

// The CharGen natives are RaceMenu's SKSE callbacks, which take (StaticFunctionTag*, arguments...). A
// BSFixedString argument is passed by value, which on x64 means a pointer to a temporary that the callee
// releases: the string handed over below carries its own reference for that.
using TSaveCharacterPreset = void (*)(void* apTag, Actor* apActor, BSFixedString* apName);
using TLoadCharacterPresetEx = bool (*)(void* apTag, Actor* apActor, BSFixedString* apName, void* apHairColor, uint32_t aFlags);

// NiOverride natives, same convention (TESObjectREFR* arguments).
using TSetBodyMorph = void (*)(void* apTag, Actor* apActor, BSFixedString* apMorph, BSFixedString* apKey, float aValue);
using TGetBodyMorph = float (*)(void* apTag, Actor* apActor, BSFixedString* apMorph, BSFixedString* apKey);
using THasBodyMorphKey = bool (*)(void* apTag, Actor* apActor, BSFixedString* apKey);
using TActorCall = void (*)(void* apTag, Actor* apActor);
using TActorQuery = bool (*)(void* apTag, Actor* apActor);

template <class T> T GetNative(World& aWorld, const char* acpType, const char* acpName)
{
    return reinterpret_cast<T>(const_cast<void*>(aWorld.ctx().at<PapyrusService>().Get(acpType, acpName)));
}

// Just enough JSON to read RaceMenu's "bodyMorphs" array; anything else is skipped over.
struct JsonReader
{
    const char* p;
    const char* end;

    void SkipWs() noexcept
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            ++p;
    }

    bool Consume(char aC) noexcept
    {
        SkipWs();
        if (p < end && *p == aC)
        {
            ++p;
            return true;
        }
        return false;
    }

    bool Peek(char aC) noexcept
    {
        SkipWs();
        return p < end && *p == aC;
    }

    bool ReadString(std::string& aOut) noexcept
    {
        aOut.clear();
        if (!Consume('"'))
            return false;
        while (p < end && *p != '"')
        {
            if (*p == '\\' && p + 1 < end)
            {
                ++p;
                switch (*p)
                {
                case 'n': aOut += '\n'; break;
                case 't': aOut += '\t'; break;
                case 'r': aOut += '\r'; break;
                case 'b': aOut += '\b'; break;
                case 'f': aOut += '\f'; break;
                case 'u':
                    // Morph names and keys are ASCII; keep the escape's low byte.
                    if (end - p >= 5)
                    {
                        aOut += static_cast<char>(std::strtoul(std::string(p + 1, p + 5).c_str(), nullptr, 16) & 0xFF);
                        p += 4;
                    }
                    break;
                default: aOut += *p; break;
                }
                ++p;
                continue;
            }
            aOut += *p++;
        }
        return Consume('"');
    }

    bool ReadNumber(double& aOut) noexcept
    {
        SkipWs();
        const char* start = p;
        while (p < end && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E'))
            ++p;
        if (p == start)
            return false;
        aOut = std::strtod(std::string(start, p).c_str(), nullptr);
        return true;
    }

    bool SkipValue(int aDepth = 0) noexcept
    {
        if (aDepth > 64)
            return false;
        SkipWs();
        if (p >= end)
            return false;
        if (*p == '"')
        {
            std::string ignored;
            return ReadString(ignored);
        }
        if (*p == '{' || *p == '[')
        {
            const char cClose = *p == '{' ? '}' : ']';
            const bool cObject = *p == '{';
            ++p;
            if (Consume(cClose))
                return true;
            do
            {
                if (cObject)
                {
                    std::string key;
                    if (!ReadString(key) || !Consume(':'))
                        return false;
                }
                if (!SkipValue(aDepth + 1))
                    return false;
            } while (Consume(','));
            return Consume(cClose);
        }
        // Number or literal.
        const char* start = p;
        while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t')
            ++p;
        return p != start;
    }
};

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
    // The hair color forms stay allocated: a copy that is still being torn down may point at one.
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
        ApplyPending(m_applyTimer);
        m_applyTimer = 0.0;
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
    preset.SettleTimer = -1.0;

    spdlog::info("RaceMenu sync: received the appearance of player {:X}, revision {}, {} bytes", acMessage.PlayerId, acMessage.Revision, preset.Json.size());
}

void RaceMenuSyncService::ParsePreset(RemotePreset& aPreset) noexcept
{
    if (aPreset.ParsedRevision == aPreset.Revision && aPreset.ParsedRevision != 0)
        return;

    aPreset.ParsedRevision = aPreset.Revision;
    aPreset.BodyMorphs.clear();
    aPreset.HasOverlayNodes = aPreset.Json.find("[Ovl") != String::npos || aPreset.Json.find("[SOvl") != String::npos;

    const auto cKey = aPreset.Json.find("\"bodyMorphs\"");
    if (cKey == String::npos)
        return;

    JsonReader reader{aPreset.Json.data() + cKey + 12, aPreset.Json.data() + aPreset.Json.size()};
    if (!reader.Consume(':') || !reader.Consume('['))
        return;
    if (reader.Consume(']'))
        return;

    do
    {
        if (!reader.Consume('{'))
            return;

        std::string name;
        std::vector<std::pair<std::string, float>> keys;
        if (!reader.Peek('}'))
        {
            do
            {
                std::string member;
                if (!reader.ReadString(member) || !reader.Consume(':'))
                    return;

                if (member == "name")
                {
                    if (!reader.ReadString(name))
                        return;
                }
                else if (member == "value") // presets from before morph keys
                {
                    double value;
                    if (!reader.ReadNumber(value))
                        return;
                    keys.emplace_back("RSMLegacy", static_cast<float>(value));
                }
                else if (member == "keys")
                {
                    if (!reader.Consume('['))
                        return;
                    if (!reader.Consume(']'))
                    {
                        do
                        {
                            if (!reader.Consume('{'))
                                return;
                            std::string key;
                            double value = 0.0;
                            do
                            {
                                std::string field;
                                if (!reader.ReadString(field) || !reader.Consume(':'))
                                    return;
                                if (field == "key")
                                {
                                    if (!reader.ReadString(key))
                                        return;
                                }
                                else if (field == "value")
                                {
                                    if (!reader.ReadNumber(value))
                                        return;
                                }
                                else if (!reader.SkipValue())
                                    return;
                            } while (reader.Consume(','));
                            if (!reader.Consume('}'))
                                return;
                            if (!key.empty())
                                keys.emplace_back(std::move(key), static_cast<float>(value));
                        } while (reader.Consume(','));
                        if (!reader.Consume(']'))
                            return;
                    }
                }
                else if (!reader.SkipValue())
                    return;
            } while (reader.Consume(','));
        }
        if (!reader.Consume('}'))
            return;

        if (!name.empty())
        {
            for (auto& [key, value] : keys)
                aPreset.BodyMorphs.push_back({String(name.c_str()), String(key.c_str()), value});
        }
    } while (reader.Consume(','));
}

void RaceMenuSyncService::ApplyPending(double aDelta) noexcept
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

        bool apply = preset.AppliedRevision != preset.Revision || preset.AppliedFormId != pActor->formID || preset.AppliedRoot != pRoot;

        // STR rebuilding the copy's base from the owner's appearance points its hair back at the shared form.
        auto* pNpc = Cast<TESNPC>(pActor->baseForm);
        // Only once the copy has been seen holding its own form, so a base RaceMenu could not repoint does not
        // turn this into a reload loop.
        if (!apply && preset.HairColorHeld && pNpc && pNpc->headData && pNpc->headData->hairColor != preset.HairColorForm)
        {
            preset.HairColorHeld = false;
            spdlog::info("RaceMenu sync: the copy of player {:X} lost its own hair color, applying the appearance again", cPlayerId);
            apply = true;
        }

        bool settlePass = false;
        if (!apply && preset.SettleTimer >= 0.0)
        {
            preset.SettleTimer -= aDelta;
            if (preset.SettleTimer < 0.0)
            {
                apply = true;
                settlePass = true;
            }
        }

        if (apply)
        {
            if (ApplyPreset(cPlayerId, pActor, preset))
            {
                // The first load after a change gets a second pass once things have settled; the second pass
                // does not schedule another.
                preset.SettleTimer = settlePass ? -1.0 : kSettleSeconds;
                preset.MorphCheckTimer = kMorphCheckSeconds;
                preset.AppliedRevision = preset.Revision;
                preset.AppliedFormId = pActor->formID;
                preset.AppliedRoot = pRoot;
                preset.HairColorHeld = preset.HairColorForm && pNpc && pNpc->headData && pNpc->headData->hairColor == preset.HairColorForm;

                // Rebuilding the head drops the complexion STR painted on it; repaint it with the owner's tints.
                if (!preset.FaceTints.Entries.empty())
                {
                    FaceGenSystem::Setup(m_world, entity, preset.FaceTints);
                }

                if (settlePass)
                    EnsureOverlays(cPlayerId, pActor, preset);
            }
            else if (++preset.FailedAttempts == kMaxApplyAttempts)
            {
                spdlog::warn("RaceMenu sync: giving up on the appearance of player {:X} after {} attempts", cPlayerId, kMaxApplyAttempts);
            }
            continue;
        }

        preset.MorphCheckTimer -= aDelta;
        if (preset.MorphCheckTimer <= 0.0)
        {
            preset.MorphCheckTimer = kMorphCheckSeconds;
            EnforceBodyMorphs(cPlayerId, pActor, preset, false);
            EnsureOverlays(cPlayerId, pActor, preset);
        }
    }
}

bool RaceMenuSyncService::ApplyPreset(uint32_t aPlayerId, Actor* apActor, RemotePreset& aPreset) noexcept
{
    const auto cLoad = GetNative<TLoadCharacterPresetEx>(m_world, "CharGen", "LoadCharacterPresetEx");
    const auto cRemoveOverlays = GetNative<TActorCall>(m_world, "NiOverride", "RemoveOverlays");

    ParsePreset(aPreset);

    const std::string cName = fmt::format("STRSync_{:X}", aPlayerId);
    const auto cPath = PresetPath(cName);
    if (!WriteFile(cPath, aPreset.Json))
    {
        spdlog::warn("RaceMenu sync: could not write {} for RaceMenu to load", cPath.string());
        return false;
    }

    // Give this copy a hair color form of its own (see the header); RaceMenu writes the preset's color into
    // the form it is handed and points the copy's base at it. One form per player, reused across respawns.
    if (!aPreset.HairColorForm)
    {
        if (auto* pFactory = IFormFactory::GetForType(kColorFormType))
        {
            if (auto* pColor = static_cast<BGSColorForm*>(pFactory->Create()))
            {
                pColor->color.red = pColor->color.green = pColor->color.blue = 0;
                pColor->color.alpha = 0xFF;
                aPreset.HairColorForm = pColor;
            }
        }
        if (!aPreset.HairColorForm)
            spdlog::warn("RaceMenu sync: could not create a hair color form for player {:X}; the copy keeps the shared one", aPlayerId);
    }

    // RaceMenu only builds overlays on a copy it has not registered, and builds them on a task after this call.
    // Dropping the registration first makes the load queue a fresh build ahead of the overlay overrides, so the
    // overrides land on overlay nodes that exist.
    if (aPreset.HasOverlayNodes && cRemoveOverlays)
        cRemoveOverlays(nullptr, apActor);

    BSFixedString name(cName.c_str()); // released by the callee
    const bool cLoaded = cLoad(nullptr, apActor, &name, aPreset.HairColorForm, kPresetApplyAll);
    RemoveFile(cPath);

    if (cLoaded)
        EnforceBodyMorphs(aPlayerId, apActor, aPreset, false);

    spdlog::info("RaceMenu sync: appearance revision {} of player {:X} {} on {:X} ({} body morphs, overlay nodes: {}, own hair color: {})",
                 aPreset.Revision, aPlayerId, cLoaded ? "applied" : "was refused by RaceMenu", apActor->formID, aPreset.BodyMorphs.size(),
                 aPreset.HasOverlayNodes, aPreset.HairColorForm != nullptr);
    return cLoaded;
}

void RaceMenuSyncService::EnforceBodyMorphs(uint32_t aPlayerId, Actor* apActor, RemotePreset& aPreset, bool aForce) noexcept
{
    const auto cSet = GetNative<TSetBodyMorph>(m_world, "NiOverride", "SetBodyMorph");
    const auto cGet = GetNative<TGetBodyMorph>(m_world, "NiOverride", "GetBodyMorph");
    const auto cHasKey = GetNative<THasBodyMorphKey>(m_world, "NiOverride", "HasBodyMorphKey");
    const auto cClear = GetNative<TActorCall>(m_world, "NiOverride", "ClearMorphs");
    const auto cUpdate = GetNative<TActorCall>(m_world, "NiOverride", "UpdateModelWeight");
    if (!cSet || !cGet || !cHasKey || !cClear || !cUpdate)
        return;

    const char* pReason = aForce ? "requested" : nullptr;

    // Morphs the copy picked up from BodyGen or OBody.
    if (!pReason)
    {
        for (const char* cpKey : kForeignMorphKeys)
        {
            const bool cOwnersKey = std::any_of(aPreset.BodyMorphs.begin(), aPreset.BodyMorphs.end(), [cpKey](const auto& acMorph) { return acMorph.Key == cpKey; });
            if (cOwnersKey)
                continue;

            BSFixedString key(cpKey); // released by the callee
            if (cHasKey(nullptr, apActor, &key))
            {
                pReason = cpKey;
                break;
            }
        }
    }

    // The owner's morphs changed or cleared underneath us.
    if (!pReason)
    {
        for (const auto& cMorph : aPreset.BodyMorphs)
        {
            BSFixedString morph(cMorph.Name.c_str());
            BSFixedString key(cMorph.Key.c_str());
            if (std::fabs(cGet(nullptr, apActor, &morph, &key) - cMorph.Value) > 0.001f)
            {
                pReason = "drift";
                break;
            }
        }
    }

    if (!pReason)
        return;

    cClear(nullptr, apActor);
    for (const auto& cMorph : aPreset.BodyMorphs)
    {
        BSFixedString morph(cMorph.Name.c_str());
        BSFixedString key(cMorph.Key.c_str());
        cSet(nullptr, apActor, &morph, &key, cMorph.Value);
    }
    cUpdate(nullptr, apActor);

    static uint32_t s_logged = 0;
    if (s_logged < 20 || s_logged % 50 == 0)
        spdlog::info("RaceMenu sync: re-applied {} body morphs of player {:X} on {:X} ({})", aPreset.BodyMorphs.size(), aPlayerId, apActor->formID, pReason);
    ++s_logged;
}

void RaceMenuSyncService::EnsureOverlays(uint32_t aPlayerId, Actor* apActor, const RemotePreset& aPreset) noexcept
{
    if (!aPreset.HasOverlayNodes)
        return;

    const auto cHas = GetNative<TActorQuery>(m_world, "NiOverride", "HasOverlays");
    const auto cAdd = GetNative<TActorCall>(m_world, "NiOverride", "AddOverlays");
    const auto cApplyNodes = GetNative<TActorCall>(m_world, "NiOverride", "ApplyNodeOverrides");
    if (!cHas || !cAdd)
        return;

    if (cHas(nullptr, apActor))
        return;

    // AddOverlays builds the nodes on a task and the build applies the copy's stored node overrides to them.
    cAdd(nullptr, apActor);
    if (cApplyNodes)
        cApplyNodes(nullptr, apActor);

    spdlog::info("RaceMenu sync: overlays were missing on the copy of player {:X} ({:X}), rebuilt them", aPlayerId, apActor->formID);
}
