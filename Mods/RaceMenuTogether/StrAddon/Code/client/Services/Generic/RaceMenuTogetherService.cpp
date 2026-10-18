#include <Services/RaceMenuTogetherService.h>

#include <RaceMenuTogether/STRRaceMenuAPI.h>

#include <Components.h>
#include <World.h>

#include <Events/UpdateEvent.h>

#include <Messages/NotifyScriptAnimation.h>
#include <Messages/ScriptAnimationRequest.h>

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

#include <compressapi.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>

// Windows' own compression (Windows 8 and later), so the add-on needs no extra library.
#pragma comment(lib, "cabinet.lib")

// Lets the plugin see that this Skyrim Together build carries the add-on.
extern "C" __declspec(dllexport) uint32_t STRRaceMenu_HostApiVersion()
{
    return STRRaceMenu::kApiVersion;
}

namespace
{
// "STRRM1|<sender's player id, hex>|<revision>|<piece>|<pieces>|<piece bytes>", relayed as a script animation for
// an object id that the stock client drops without playing anything. 1.8.5 resolves that id as a server id, so
// one that cannot exist is used; 1.8.3 skips id 0 outright (apply_addon.py --protocol 1.8.3 defines it).
#ifndef STR_RACEMENU_NO_OBJECT
#define STR_RACEMENU_NO_OBJECT 0xFFFFFFFFu
#endif
constexpr const char* kWirePrefix = "STRRM1|";
constexpr uint32_t kNoObject = STR_RACEMENU_NO_OBJECT;

// Script animation strings carry a 16-bit length, so an appearance goes out in pieces below that.
constexpr size_t kPieceBytes = 60000;
constexpr uint32_t kMaxPieces = 8;

// The appearance as sent: "RMT1", the preset's size, the face tints, then the compressed preset.
constexpr char kBlobMagic[4] = {'R', 'M', 'T', '1'};

// RaceMenu reads and writes presets here, relative to the game folder (Mod Organizer redirects new files
// into its overwrite folder). The files only exist for the duration of one save or one load.
constexpr const char* kPresetFolder = "Data\\SKSE\\Plugins\\CharGen\\Presets\\";
constexpr const char* kLocalPresetName = "RaceMenuTogether_Local";

// RaceMenu's ApplyTypes: face, overrides, body morphs, transforms and skin overrides.
constexpr uint32_t kPresetApplyAll = 0x0F;

// Larger compressed presets than this are not sent (one piece is left for the header and the face tints).
constexpr size_t kMaxCompressedBytes = kPieceBytes * (kMaxPieces - 1);

// Give the character a moment to be assigned before the first export after joining.
constexpr double kJoinDelaySeconds = 5.0;
// A player who just showed up gets everybody's appearance this long after, so a few arriving together share
// one round.
constexpr double kResendDelaySeconds = 3.0;
constexpr double kApplyIntervalSeconds = 0.5;
constexpr uint32_t kMaxApplyAttempts = 5;

// A second full pass this long after a load, once RaceMenu's queued overlay builds and the copy's equipment have
// settled, and how often the copy's body morphs are compared against the owner's.
constexpr double kSettleSeconds = 4.0;
constexpr double kMorphCheckSeconds = 5.0;

// The plugin can load late, so keep looking for it for a while.
constexpr double kScanInterval = 5.0;
constexpr double kScanDuration = 600.0;

// BGSColorForm (CLFM).
constexpr auto kColorFormType = static_cast<FormType>(0x85);

// Used when the plugin gives no keys of its own.
constexpr const char* kDefaultProtectedMorphKeys[] = {"RSMBodyGen", "OBody", "OBodyNG"};

// The CharGen and NiOverride natives are RaceMenu's SKSE callbacks, which take (StaticFunctionTag*, arguments...).
// SKSE's BSFixedString is a plain struct holding the interned string pointer, so a BSFixedString argument
// travels by value in a register as that pointer, and the callee does not release it. Pass NativeString (the
// same layout) and release our own BSFixedString afterwards.
struct NativeString
{
    const char* data;
};
static_assert(sizeof(NativeString) == sizeof(void*) && std::is_trivially_copyable_v<NativeString>);

NativeString Arg(const BSFixedString& acString) noexcept
{
    return NativeString{acString.data};
}

using TSaveCharacterPreset = void (*)(void* apTag, Actor* apActor, NativeString aName);
using TLoadCharacterPresetEx = bool (*)(void* apTag, Actor* apActor, NativeString aName, void* apHairColor, uint32_t aFlags);

// NiOverride natives, same convention (TESObjectREFR* arguments).
using TSetBodyMorph = void (*)(void* apTag, Actor* apActor, NativeString aMorph, NativeString aKey, float aValue);
using TGetBodyMorph = float (*)(void* apTag, Actor* apActor, NativeString aMorph, NativeString aKey);
using THasBodyMorphKey = bool (*)(void* apTag, Actor* apActor, NativeString aKey);
using TActorCall = void (*)(void* apTag, Actor* apActor);
using TActorQuery = bool (*)(void* apTag, Actor* apActor);

template <class T> T GetNative(World& aWorld, const char* acpType, const char* acpName)
{
    return reinterpret_cast<T>(const_cast<void*>(aWorld.ctx().at<PapyrusService>().Get(acpType, acpName)));
}

// --- byte packing --------------------------------------------------------------------------------------------

void PutU32(String& aOut, uint32_t aValue)
{
    for (int i = 0; i < 4; ++i)
        aOut.push_back(static_cast<char>((aValue >> (8 * i)) & 0xFF));
}

void PutF32(String& aOut, float aValue)
{
    uint32_t bits;
    std::memcpy(&bits, &aValue, sizeof(bits));
    PutU32(aOut, bits);
}

struct ByteReader
{
    const char* p;
    const char* end;

    bool U32(uint32_t& aOut) noexcept
    {
        if (end - p < 4)
            return false;
        aOut = 0;
        for (int i = 0; i < 4; ++i)
            aOut |= static_cast<uint32_t>(static_cast<uint8_t>(p[i])) << (8 * i);
        p += 4;
        return true;
    }

    bool F32(float& aOut) noexcept
    {
        uint32_t bits;
        if (!U32(bits))
            return false;
        std::memcpy(&aOut, &bits, sizeof(aOut));
        return true;
    }

    bool Bytes(size_t aCount, String& aOut) noexcept
    {
        if (static_cast<size_t>(end - p) < aCount)
            return false;
        aOut.assign(p, p + aCount);
        p += aCount;
        return true;
    }
};

// --- compression ---------------------------------------------------------------------------------------------

bool CompressPreset(const std::string& acInput, String& aOut)
{
    COMPRESSOR_HANDLE handle = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &handle))
        return false;

    SIZE_T size = 0;
    ::Compress(handle, acInput.data(), acInput.size(), nullptr, 0, &size);
    bool ok = size != 0;
    if (ok)
    {
        aOut.resize(size);
        ok = ::Compress(handle, acInput.data(), acInput.size(), aOut.data(), aOut.size(), &size) != FALSE;
        aOut.resize(ok ? size : 0);
    }
    CloseCompressor(handle);
    return ok;
}

bool DecompressPreset(const char* acpInput, size_t aInputSize, uint32_t aSize, String& aOut)
{
    // A preset is JSON text; refuse anything that claims to be implausibly large.
    if (aSize == 0 || aSize > 16 * 1024 * 1024)
        return false;

    DECOMPRESSOR_HANDLE handle = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &handle))
        return false;

    aOut.resize(aSize);
    SIZE_T size = 0;
    const bool cOk = ::Decompress(handle, acpInput, aInputSize, aOut.data(), aOut.size(), &size) != FALSE;
    CloseDecompressor(handle);
    aOut.resize(cOk ? size : 0);
    return cOk && size == aSize;
}

// --- RaceMenu's preset files ---------------------------------------------------------------------------------

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

// --- face tints ----------------------------------------------------------------------------------------------

void PutLocalTints(String& aOut)
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
    {
        PutU32(aOut, 0);
        return;
    }

    const auto& gameTints = pPlayer->GetTints();
    PutU32(aOut, gameTints.length);
    for (auto i = 0u; i < gameTints.length; ++i)
    {
        const TintMask* pTint = gameTints[i];
        PutF32(aOut, pTint->alpha);
        PutU32(aOut, pTint->color);
        PutU32(aOut, pTint->type);
        const char* pName = pTint->texture ? pTint->texture->name.AsAscii() : "";
        const size_t cLength = pName ? strlen(pName) : 0;
        PutU32(aOut, static_cast<uint32_t>(cLength));
        aOut.append(pName ? pName : "", cLength);
    }
}

bool ReadTints(ByteReader& aReader, Tints& aOut)
{
    uint32_t count = 0;
    if (!aReader.U32(count) || count > 512)
        return false;

    aOut.Entries.clear();
    aOut.Entries.resize(count);
    for (auto& entry : aOut.Entries)
    {
        uint32_t length = 0;
        String name;
        if (!aReader.F32(entry.Alpha) || !aReader.U32(entry.Color) || !aReader.U32(entry.Type) || !aReader.U32(length) || length > 1024 || !aReader.Bytes(length, name))
            return false;
        entry.Name = name;
    }
    return true;
}

bool StartsWith(const String& acText, const char* acpPrefix) noexcept
{
    return acText.compare(0, strlen(acpPrefix), acpPrefix) == 0;
}
} // namespace

RaceMenuTogetherService::RaceMenuTogetherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&RaceMenuTogetherService::OnUpdate>(this);
    m_notifyConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&RaceMenuTogetherService::OnNotifyScriptAnimation>(this);
}

void RaceMenuTogetherService::ScanForPlugin() noexcept
{
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return;

    const size_t cCount = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));
    for (size_t i = 0; i < cCount; ++i)
    {
        const auto getDescriptor = reinterpret_cast<STRRaceMenu::TGetDescriptor>(GetProcAddress(modules[i], STRRaceMenu::kDescriptorExport));
        if (!getDescriptor)
            continue;

        char path[MAX_PATH]{};
        K32GetModuleBaseNameA(GetCurrentProcess(), modules[i], path, MAX_PATH);

        const STRRaceMenu::Descriptor* pDescriptor = getDescriptor();
        if (!pDescriptor || pDescriptor->ApiVersion != STRRaceMenu::kApiVersion)
        {
            spdlog::warn("RaceMenu Together: {} has a descriptor this add-on cannot use (API {}, add-on API {})", path, pDescriptor ? pDescriptor->ApiVersion : 0,
                         STRRaceMenu::kApiVersion);
            continue;
        }

        m_protectedMorphKeys.clear();
        if (pDescriptor->ProtectedMorphKeys && pDescriptor->ProtectedMorphKeyCount <= STRRaceMenu::kMaxProtectedMorphKeys)
        {
            for (uint32_t k = 0; k < pDescriptor->ProtectedMorphKeyCount; ++k)
            {
                const char* pKey = pDescriptor->ProtectedMorphKeys[k];
                if (pKey && *pKey && strlen(pKey) < 128)
                    m_protectedMorphKeys.push_back(pKey);
            }
        }
        if (m_protectedMorphKeys.empty())
        {
            for (const char* cpKey : kDefaultProtectedMorphKeys)
                m_protectedMorphKeys.push_back(cpKey);
        }

        const uint32_t cInterfaceVersion = pDescriptor->GetRaceMenuInterfaceVersion ? pDescriptor->GetRaceMenuInterfaceVersion() : 0;
        spdlog::info("RaceMenu Together: {} {} ({}) found, {} protected morph key(s), RaceMenu body morph interface {}",
                     pDescriptor->ModName ? pDescriptor->ModName : "?", pDescriptor->ModVersion ? pDescriptor->ModVersion : "", path, m_protectedMorphKeys.size(),
                     cInterfaceVersion ? fmt::format("v{}", cInterfaceVersion) : std::string("not reported"));

        m_pluginFound = true;
        return;
    }
}

bool RaceMenuTogetherService::RaceMenuAvailable() noexcept
{
    auto& papyrus = m_world.ctx().at<PapyrusService>();
    const bool cAvailable = papyrus.Get("CharGen", "SaveCharacterPreset") && papyrus.Get("CharGen", "LoadCharacterPresetEx");
    if (!cAvailable && !m_reportedMissing)
    {
        m_reportedMissing = true;
        spdlog::info("RaceMenu Together is off: RaceMenu's CharGen.SaveCharacterPreset/LoadCharacterPresetEx are not registered");
    }
    return cAvailable;
}

void RaceMenuTogetherService::Reset() noexcept
{
    // The hair color forms stay allocated: a copy that is still being torn down may point at one.
    m_remotePresets.clear();
    m_incoming.clear();
    m_knownPlayers.clear();
    m_resendTimer = -1.0;
    m_lastBlob.clear();
    m_sendAfterJoin = false;
}

void RaceMenuTogetherService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_pluginFound && m_scanElapsed < kScanDuration)
    {
        m_scanElapsed += acEvent.Delta;
        m_scanTimer -= acEvent.Delta;
        if (m_scanTimer <= 0.0)
        {
            m_scanTimer = kScanInterval;
            ScanForPlugin();
        }
    }
    if (!m_pluginFound)
        return;

    const bool cOnline = m_transport.IsOnline();
    if (cOnline != m_wasOnline)
    {
        m_wasOnline = cOnline;
        Reset();
        if (cOnline)
        {
            m_sendAfterJoin = true;
            m_joinDelay = 0.0;
        }
    }
    if (!cOnline)
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
        WatchForNewPlayers(m_applyTimer);
        ApplyPending(m_applyTimer);
        m_applyTimer = 0.0;
    }
}

void RaceMenuTogetherService::WatchForNewPlayers(double aDelta) noexcept
{
    bool newPlayer = false;
    auto view = m_world.view<PlayerComponent>();
    for (auto entity : view)
    {
        const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
        if (cPlayerId == m_transport.GetLocalPlayerId())
            continue;
        if (m_knownPlayers.insert(cPlayerId).second)
            newPlayer = true;
    }

    // The server does not keep appearances, so whoever was here already sends theirs again.
    if (newPlayer && m_resendTimer < 0.0)
        m_resendTimer = kResendDelaySeconds;

    if (m_resendTimer >= 0.0)
    {
        m_resendTimer -= aDelta;
        if (m_resendTimer < 0.0 && !m_sendAfterJoin)
        {
            if (!m_lastBlob.empty())
            {
                spdlog::info("RaceMenu Together: a player joined, sending the player's appearance again (revision {})", m_lastRevision);
                SendBlob(m_lastBlob, m_lastRevision);
            }
            else
            {
                SendLocalPreset("a player joined");
            }
        }
    }
}

void RaceMenuTogetherService::SendLocalPreset(const char* acpReason) noexcept
{
    if (!RaceMenuAvailable())
        return;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const auto cSave = GetNative<TSaveCharacterPreset>(m_world, "CharGen", "SaveCharacterPreset");

    const auto cPath = PresetPath(kLocalPresetName);
    RemoveFile(cPath);

    BSFixedString name(kLocalPresetName);
    cSave(nullptr, pPlayer, Arg(name));
    name.Release();

    std::string json;
    const bool cRead = ReadFile(cPath, json);
    RemoveFile(cPath);
    if (!cRead)
    {
        spdlog::warn("RaceMenu Together: RaceMenu did not write the player's preset ({})", acpReason);
        return;
    }

    String compressed;
    if (!CompressPreset(json, compressed) || compressed.size() > kMaxCompressedBytes)
    {
        spdlog::warn("RaceMenu Together: the player's preset is too large to send ({} bytes, {} compressed)", json.size(), compressed.size());
        return;
    }

    String blob(kBlobMagic, sizeof(kBlobMagic));
    PutU32(blob, static_cast<uint32_t>(json.size()));
    PutLocalTints(blob);
    blob += compressed;

    m_lastBlob = std::move(blob);
    m_lastRevision = ++m_localRevision;

    spdlog::info("RaceMenu Together: sending the player's appearance ({}), revision {}, {} bytes ({} compressed)", acpReason, m_lastRevision, json.size(),
                 compressed.size());
    SendBlob(m_lastBlob, m_lastRevision);
}

void RaceMenuTogetherService::SendBlob(const String& acBlob, uint32_t aRevision) noexcept
{
    const uint32_t cPieces = static_cast<uint32_t>((acBlob.size() + kPieceBytes - 1) / kPieceBytes);
    if (cPieces == 0 || cPieces > kMaxPieces)
        return;

    for (uint32_t i = 0; i < cPieces; ++i)
    {
        String wire = fmt::format("{}{:X}|{}|{}|{}|", kWirePrefix, m_transport.GetLocalPlayerId(), aRevision, i, cPieces).c_str();
        const size_t cOffset = static_cast<size_t>(i) * kPieceBytes;
        wire.append(acBlob, cOffset, std::min(kPieceBytes, acBlob.size() - cOffset));

        ScriptAnimationRequest request{};
        request.FormID = kNoObject;
        request.EventName = wire;
        m_transport.Send(request);
    }
}

void RaceMenuTogetherService::OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept
{
    const String& cWire = acMessage.EventName;
    if (!m_pluginFound || !StartsWith(cWire, kWirePrefix))
        return;

    // "<player id>|<revision>|<piece>|<pieces>|" then the piece's bytes.
    uint32_t fields[4]{};
    size_t position = strlen(kWirePrefix);
    for (int f = 0; f < 4; ++f)
    {
        const size_t cBar = cWire.find('|', position);
        if (cBar == String::npos || cBar == position || cBar - position > 10)
            return;
        fields[f] = static_cast<uint32_t>(strtoul(cWire.substr(position, cBar - position).c_str(), nullptr, f == 0 ? 16 : 10));
        position = cBar + 1;
    }

    const uint32_t cPlayerId = fields[0];
    const uint32_t cRevision = fields[1];
    const uint32_t cPiece = fields[2];
    const uint32_t cPieces = fields[3];

    // 1.8.3 servers send our own pieces back to us.
    if (cPlayerId == m_transport.GetLocalPlayerId() || cPieces == 0 || cPieces > kMaxPieces || cPiece >= cPieces)
        return;

    auto& incoming = m_incoming[cPlayerId];
    if (incoming.Revision != cRevision || incoming.Count != cPieces)
    {
        incoming = Incoming{};
        incoming.Revision = cRevision;
        incoming.Count = cPieces;
        incoming.Pieces.resize(cPieces);
    }

    if (incoming.Pieces[cPiece].empty())
    {
        incoming.Pieces[cPiece] = cWire.substr(position);
        if (!incoming.Pieces[cPiece].empty())
            ++incoming.Received;
    }

    if (incoming.Received < incoming.Count)
        return;

    String blob;
    for (const auto& piece : incoming.Pieces)
        blob += piece;
    m_incoming.erase(cPlayerId);

    OnBlob(cPlayerId, cRevision, blob);
}

void RaceMenuTogetherService::OnBlob(uint32_t aPlayerId, uint32_t aRevision, const String& acBlob) noexcept
{
    // Sent again for a player who joined after us: nothing new for a copy that already has it.
    auto existing = m_remotePresets.find(aPlayerId);
    if (existing != m_remotePresets.end() && existing->second.Revision == aRevision && !existing->second.Json.empty())
        return;

    ByteReader reader{acBlob.data(), acBlob.data() + acBlob.size()};
    String magic;
    uint32_t size = 0;
    Tints tints;
    String json;
    if (!reader.Bytes(sizeof(kBlobMagic), magic) || std::memcmp(magic.data(), kBlobMagic, sizeof(kBlobMagic)) != 0 || !reader.U32(size) || !ReadTints(reader, tints) ||
        !DecompressPreset(reader.p, static_cast<size_t>(reader.end - reader.p), size, json))
    {
        spdlog::warn("RaceMenu Together: could not unpack the appearance of player {:X}", aPlayerId);
        return;
    }

    auto& preset = m_remotePresets[aPlayerId];
    preset.Revision = aRevision;
    preset.Json = std::move(json);
    preset.FaceTints = std::move(tints);
    preset.FailedAttempts = 0;
    preset.SettleTimer = -1.0;

    spdlog::info("RaceMenu Together: received the appearance of player {:X}, revision {}, {} bytes", aPlayerId, aRevision, preset.Json.size());
}

void RaceMenuTogetherService::ParsePreset(RemotePreset& aPreset) noexcept
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

void RaceMenuTogetherService::ApplyPending(double aDelta) noexcept
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
            spdlog::info("RaceMenu Together: the copy of player {:X} lost its own hair color, applying the appearance again", cPlayerId);
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
                    FaceGenSystem::Setup(m_world, entity, preset.FaceTints);

                if (settlePass)
                    EnsureOverlays(cPlayerId, pActor, preset);
            }
            else if (++preset.FailedAttempts == kMaxApplyAttempts)
            {
                spdlog::warn("RaceMenu Together: giving up on the appearance of player {:X} after {} attempts", cPlayerId, kMaxApplyAttempts);
            }
            continue;
        }

        preset.MorphCheckTimer -= aDelta;
        if (preset.MorphCheckTimer <= 0.0)
        {
            preset.MorphCheckTimer = kMorphCheckSeconds;
            EnforceBodyMorphs(cPlayerId, pActor, preset);
            EnsureOverlays(cPlayerId, pActor, preset);
        }
    }
}

bool RaceMenuTogetherService::ApplyPreset(uint32_t aPlayerId, Actor* apActor, RemotePreset& aPreset) noexcept
{
    const auto cLoad = GetNative<TLoadCharacterPresetEx>(m_world, "CharGen", "LoadCharacterPresetEx");
    const auto cRemoveOverlays = GetNative<TActorCall>(m_world, "NiOverride", "RemoveOverlays");

    ParsePreset(aPreset);

    const std::string cName = fmt::format("RaceMenuTogether_{:X}", aPlayerId);
    const auto cPath = PresetPath(cName);
    if (!WriteFile(cPath, aPreset.Json))
    {
        spdlog::warn("RaceMenu Together: could not write {} for RaceMenu to load", cPath.string());
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
            spdlog::warn("RaceMenu Together: could not create a hair color form for player {:X}; the copy keeps the shared one", aPlayerId);
    }

    // RaceMenu only builds overlays on a copy it has not registered, and builds them on a task after this call.
    // Dropping the registration first makes the load queue a fresh build ahead of the overlay overrides, so the
    // overrides land on overlay nodes that exist.
    if (aPreset.HasOverlayNodes && cRemoveOverlays)
        cRemoveOverlays(nullptr, apActor);

    BSFixedString name(cName.c_str());
    const bool cLoaded = cLoad(nullptr, apActor, Arg(name), aPreset.HairColorForm, kPresetApplyAll);
    name.Release();
    RemoveFile(cPath);

    if (cLoaded)
        EnforceBodyMorphs(aPlayerId, apActor, aPreset);

    spdlog::info("RaceMenu Together: appearance revision {} of player {:X} {} on {:X} ({} body morphs, overlay nodes: {}, own hair color: {})", aPreset.Revision,
                 aPlayerId, cLoaded ? "applied" : "was refused by RaceMenu", apActor->formID, aPreset.BodyMorphs.size(), aPreset.HasOverlayNodes,
                 aPreset.HairColorForm != nullptr);
    return cLoaded;
}

void RaceMenuTogetherService::EnforceBodyMorphs(uint32_t aPlayerId, Actor* apActor, RemotePreset& aPreset) noexcept
{
    const auto cSet = GetNative<TSetBodyMorph>(m_world, "NiOverride", "SetBodyMorph");
    const auto cGet = GetNative<TGetBodyMorph>(m_world, "NiOverride", "GetBodyMorph");
    const auto cHasKey = GetNative<THasBodyMorphKey>(m_world, "NiOverride", "HasBodyMorphKey");
    const auto cClear = GetNative<TActorCall>(m_world, "NiOverride", "ClearMorphs");
    const auto cUpdate = GetNative<TActorCall>(m_world, "NiOverride", "UpdateModelWeight");
    if (!cSet || !cGet || !cHasKey || !cClear || !cUpdate)
        return;

    const char* pReason = nullptr;

    // Morphs the copy picked up from BodyGen, OBody and the like.
    for (const auto& cKeyName : m_protectedMorphKeys)
    {
        const bool cOwnersKey = std::any_of(aPreset.BodyMorphs.begin(), aPreset.BodyMorphs.end(), [&cKeyName](const auto& acMorph) { return acMorph.Key == cKeyName; });
        if (cOwnersKey)
            continue;

        BSFixedString key(cKeyName.c_str());
        const bool cHasForeignKey = cHasKey(nullptr, apActor, Arg(key));
        key.Release();
        if (cHasForeignKey)
        {
            pReason = cKeyName.c_str();
            break;
        }
    }

    // The owner's morphs changed or cleared underneath us.
    if (!pReason)
    {
        for (const auto& cMorph : aPreset.BodyMorphs)
        {
            BSFixedString morph(cMorph.Name.c_str());
            BSFixedString key(cMorph.Key.c_str());
            const float cValue = cGet(nullptr, apActor, Arg(morph), Arg(key));
            morph.Release();
            key.Release();
            if (std::fabs(cValue - cMorph.Value) > 0.001f)
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
        cSet(nullptr, apActor, Arg(morph), Arg(key), cMorph.Value);
        morph.Release();
        key.Release();
    }
    cUpdate(nullptr, apActor);

    static uint32_t s_logged = 0;
    if (s_logged < 20 || s_logged % 50 == 0)
        spdlog::info("RaceMenu Together: re-applied {} body morphs of player {:X} on {:X} ({})", aPreset.BodyMorphs.size(), aPlayerId, apActor->formID, pReason);
    ++s_logged;
}

void RaceMenuTogetherService::EnsureOverlays(uint32_t aPlayerId, Actor* apActor, const RemotePreset& aPreset) noexcept
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

    spdlog::info("RaceMenu Together: overlays were missing on the copy of player {:X} ({:X}), rebuilt them", aPlayerId, apActor->formID);
}
