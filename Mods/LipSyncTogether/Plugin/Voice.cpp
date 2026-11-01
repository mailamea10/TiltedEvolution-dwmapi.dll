#include "Voice.h"

#include "Analyzer.h"

#include <opus.h>

#include <bcrypt.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace LipSync::Voice
{
namespace
{
// Opus at 48 kHz, mono, 20 ms frames.
constexpr uint32_t kRate = 48000;
constexpr uint32_t kFrameSamples = 960;
constexpr int kMaxDecodeSamples = 5760; // 120 ms, the longest frame Opus can return

// Every packet starts with this header. The relay forwards audio packets unchanged, so a receiver reads the
// speaker's player id, and how far their voice carries right now, from it.
constexpr char kMagic[4] = {'L', 'S', 'V', '2'};
enum PacketType : uint8_t
{
    kHello = 1,   // client -> relay, every 2 s: "I am this player" (also keeps the relay's entry alive)
    kAudio = 2,   // client -> relay -> every other client: one Opus frame
    kBye = 3,     // client -> relay: leaving
    kWelcome = 4, // relay -> client: answer to a hello
};
constexpr uint8_t kFlagLast = 1; // the last frame of a stretch of talking

#pragma pack(push, 1)
struct Header
{
    char Magic[4];
    uint8_t Type;
    uint8_t Flags;
    uint16_t Sequence;
    uint32_t PlayerId;
    uint8_t Range; // audio: the speaker's range multiplier x 50 (50 = normal, 125 = a 2.5x yell, 10 = a 0.2x whisper)
    uint8_t Reserved[3];
};
#pragma pack(pop)
static_assert(sizeof(Header) == 16);

constexpr float kRangeUnit = 50.f;

// A hello carries the client's voice key right after the header; a relay that requires keys (RequireVoiceKey) answers
// only hellos whose key reached it through the Skyrim Together server, from the player that key belongs to.
constexpr size_t kKeySize = 16;
using VoiceKey = std::array<uint8_t, kKeySize>;
constexpr size_t kMaxTurnedAway = 64; // addresses remembered so each is logged once

constexpr size_t kMaxPacket = 1400;
constexpr ULONGLONG kHelloInterval = 2000;
constexpr ULONGLONG kRelayTimeout = 10000;   // the relay forgets a client that has been quiet this long
constexpr ULONGLONG kNoAnswerWarning = 10000; // a client warns once if the relay never answers
constexpr ULONGLONG kSpeakerExpiry = 30000;   // a speaker unheard this long is dropped (decoder freed)
constexpr ULONGLONG kMouthActive = 1500;      // GetRemoteMouth answers for this long after a voice was heard

// Playback starts once two frames (40 ms) are waiting, and the queue is cut back when it grows past eight (160 ms),
// so network hiccups cost a little delay instead of gaps, and the delay cannot pile up.
constexpr size_t kStartFrames = 2;
constexpr size_t kMaxQueuedFrames = 8;

bool StartsWithMagic(const char* apData, int aSize) noexcept
{
    return aSize >= static_cast<int>(sizeof(Header)) && std::memcmp(apData, kMagic, sizeof(kMagic)) == 0;
}

Header MakeHeader(PacketType aType, uint32_t aPlayerId, uint16_t aSequence = 0, uint8_t aFlags = 0, float aRange = 1.f) noexcept
{
    Header header{};
    std::memcpy(header.Magic, kMagic, sizeof(kMagic));
    header.Type = aType;
    header.Flags = aFlags;
    header.Sequence = aSequence;
    header.PlayerId = aPlayerId;
    header.Range = static_cast<uint8_t>(std::clamp(std::lround(aRange * kRangeUnit), 1l, 255l));
    return header;
}

bool SameAddress(const sockaddr_in& acA, const sockaddr_in& acB) noexcept
{
    return acA.sin_addr.s_addr == acB.sin_addr.s_addr && acA.sin_port == acB.sin_port;
}

std::string Describe(const sockaddr_in& acAddress)
{
    char text[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &acAddress.sin_addr, text, sizeof(text));
    return std::string(text) + ":" + std::to_string(ntohs(acAddress.sin_port));
}

bool EnsureWinsock() noexcept
{
    static std::once_flag s_once;
    static bool s_ready = false;
    std::call_once(s_once, [] {
        WSADATA data;
        s_ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!s_ready)
            Log("Voice chat: Windows networking (Winsock) is unavailable");
    });
    return s_ready;
}

// Streaming band-limited resampler between two rates, for microphones and speakers that do not run at 48 kHz
// (44.1 kHz is common). Each output sample is a 32-tap windowed-sinc interpolation of the input, with the cut-off
// lowered when going down in rate so nothing folds back; the taps for 256 fractional positions are computed once and
// interpolated between. Equal rates pass straight through.
class Resampler
{
public:
    static constexpr int kHalfTaps = 16;
    static constexpr int kPhases = 256;

    double Step{1.0}; // input samples per output sample

    void Reset(uint32_t aFrom, uint32_t aTo)
    {
        Step = static_cast<double>(aFrom) / aTo;
        m_passThrough = aFrom == aTo;
        m_buffer.assign(kHalfTaps, 0.f);
        m_position = kHalfTaps;
        if (m_passThrough)
            return;

        // Blackman-windowed sinc, normalised per phase so a steady level passes unchanged.
        const double cCutoff = std::min(1.0, 1.0 / Step) * 0.95;
        m_taps.assign(static_cast<size_t>(kPhases + 1) * 2 * kHalfTaps, 0.f);
        for (int phase = 0; phase <= kPhases; ++phase)
        {
            const double cFraction = static_cast<double>(phase) / kPhases;
            float* pTaps = &m_taps[static_cast<size_t>(phase) * 2 * kHalfTaps];
            double sum = 0.0;
            for (int j = -kHalfTaps + 1; j <= kHalfTaps; ++j)
            {
                const double cX = j - cFraction;
                const double cArgument = 3.14159265358979 * cCutoff * cX;
                const double cSinc = std::fabs(cArgument) < 1e-9 ? 1.0 : std::sin(cArgument) / cArgument;
                const double cW = 3.14159265358979 * (cX + kHalfTaps) / (2.0 * kHalfTaps);
                const double cWindow = 0.42 - 0.5 * std::cos(2.0 * cW) + 0.08 * std::cos(4.0 * cW);
                const double cTap = cSinc * std::max(0.0, cWindow);
                pTaps[j + kHalfTaps - 1] = static_cast<float>(cTap);
                sum += cTap;
            }
            for (int k = 0; k < 2 * kHalfTaps; ++k)
                pTaps[k] = static_cast<float>(pTaps[k] / sum);
        }
    }

    template <class Out> void Push(float aSample, Out&& aOut)
    {
        if (m_passThrough)
        {
            aOut(aSample);
            return;
        }
        m_buffer.push_back(aSample);
        while (static_cast<size_t>(m_position) + kHalfTaps < m_buffer.size())
        {
            const size_t cIndex = static_cast<size_t>(m_position);
            const double cFraction = m_position - static_cast<double>(cIndex);
            const double cPhase = cFraction * kPhases;
            const int cLow = static_cast<int>(cPhase);
            const float cBlend = static_cast<float>(cPhase - cLow);
            const float* pA = &m_taps[static_cast<size_t>(cLow) * 2 * kHalfTaps];
            const float* pB = pA + 2 * kHalfTaps;
            const float* pX = &m_buffer[cIndex - kHalfTaps + 1];
            float a = 0.f, b = 0.f;
            for (int k = 0; k < 2 * kHalfTaps; ++k)
            {
                a += pX[k] * pA[k];
                b += pX[k] * pB[k];
            }
            aOut(a + (b - a) * cBlend);
            m_position += Step;
        }
        // Drop input that no output needs any more.
        const size_t cUsed = static_cast<size_t>(m_position) - kHalfTaps;
        if (cUsed > 4096)
        {
            m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(cUsed));
            m_position -= static_cast<double>(cUsed);
        }
    }

private:
    bool m_passThrough{true};
    std::vector<float> m_buffer;
    double m_position{kHalfTaps};
    std::vector<float> m_taps;
};

// --- session -------------------------------------------------------------------------------------------------

std::mutex s_sessionLock;
std::string s_sessionHost;
uint32_t s_sessionPlayer = 0;
uint64_t s_sessionGeneration = 0;
std::atomic<bool> s_threadsStarted{false};

// The socket audio is sent on, and where to; set by the network thread, read by the capture thread.
std::mutex s_sendLock;
SOCKET s_socket = INVALID_SOCKET;
sockaddr_in s_relayAddress{};
uint32_t s_localPlayer = 0;
bool s_connected = false;

std::atomic<bool> s_othersMuted{false};
std::atomic<bool> s_welcomed{false};
std::atomic<bool> s_bitrateChanged{false};

// --- voice keys ---------------------------------------------------------------------------------------------

std::mutex s_keysLock;
std::map<uint32_t, VoiceKey> s_playerKeys; // this game's own and the ones relayed by the add-on

// This game's key: random, made once per game.
const VoiceKey& LocalKey() noexcept
{
    static VoiceKey s_key{};
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, s_key.data(), static_cast<ULONG>(s_key.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        {
            // Should never happen; still unguessable from outside, only weaker.
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            const uint64_t cMix = static_cast<uint64_t>(counter.QuadPart) ^ (static_cast<uint64_t>(GetCurrentProcessId()) << 32) ^ GetTickCount64();
            for (size_t i = 0; i < s_key.size(); ++i)
                s_key[i] = static_cast<uint8_t>(cMix >> ((i % 8) * 8)) ^ static_cast<uint8_t>(i * 151);
            Log("Voice chat: the system random generator failed; using a weaker voice key");
        }
    });
    return s_key;
}

// Whether a hello (header plus key) comes from a player whose key arrived through the Skyrim Together server.
bool HasValidKey(uint32_t aPlayerId, const char* apPacket, int aSize) noexcept
{
    if (aSize < static_cast<int>(sizeof(Header) + kKeySize))
        return false;
    std::lock_guard guard(s_keysLock);
    const auto cIt = s_playerKeys.find(aPlayerId);
    return cIt != s_playerKeys.end() && std::memcmp(cIt->second.data(), apPacket + sizeof(Header), kKeySize) == 0;
}

// --- relay (HostVoiceRelay) ----------------------------------------------------------------------------------

std::atomic<bool> s_relayRunning{false};
std::atomic<bool> s_relayStop{false};
std::atomic<uint32_t> s_relayClients{0};
std::atomic<uint32_t> s_relayTurnedAway{0};
std::atomic<bool> s_hostingWanted{false}; // HostVoiceRelay asks for a relay in this session
HANDLE s_relayThread = nullptr; // network thread only
uint16_t s_relayPort = 0;

struct RelayClient
{
    sockaddr_in Address{};
    uint32_t PlayerId{};
    ULONGLONG LastSeen{};
};

DWORD WINAPI RelayThread(LPVOID apSocket)
{
    const SOCKET cSocket = reinterpret_cast<SOCKET>(apSocket);
    std::vector<RelayClient> clients;
    std::vector<sockaddr_in> turnedAway;
    char buffer[kMaxPacket];
    ULONGLONG lastPrune = GetTickCount64();

    while (!s_relayStop.load())
    {
        s_relayClients = static_cast<uint32_t>(clients.size());
        fd_set read;
        FD_ZERO(&read);
        FD_SET(cSocket, &read);
        timeval timeout{0, 200000};
        if (select(0, &read, nullptr, nullptr, &timeout) > 0)
        {
            sockaddr_in from{};
            int fromSize = sizeof(from);
            const int cSize = recvfrom(cSocket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromSize);
            if (cSize > 0 && StartsWithMagic(buffer, cSize))
            {
                Header header;
                std::memcpy(&header, buffer, sizeof(header));
                auto client = std::find_if(clients.begin(), clients.end(), [&](const RelayClient& c) { return SameAddress(c.Address, from); });
                const ULONGLONG cNow = GetTickCount64();

                if (header.Type == kHello && g_config.RequireVoiceKey && !HasValidKey(header.PlayerId, buffer, cSize))
                {
                    // No answer: a stranger, or a player whose key has not come through Skyrim Together yet (their
                    // next hello, 2 s later, gets in then).
                    if (turnedAway.size() < kMaxTurnedAway &&
                        std::none_of(turnedAway.begin(), turnedAway.end(), [&](const sockaddr_in& a) { return SameAddress(a, from); }))
                    {
                        turnedAway.push_back(from);
                        ++s_relayTurnedAway;
                        Log("Voice relay: not letting in player %X from %s: no voice key from this Skyrim Together server (yet)", header.PlayerId,
                            Describe(from).c_str());
                    }
                }
                else if (header.Type == kHello)
                {
                    if (client == clients.end())
                    {
                        clients.push_back(RelayClient{from, header.PlayerId, cNow});
                        Log("Voice relay: player %X joined from %s (%zu connected)", header.PlayerId, Describe(from).c_str(), clients.size());
                    }
                    else
                    {
                        client->PlayerId = header.PlayerId;
                        client->LastSeen = cNow;
                    }
                    const Header cWelcome = MakeHeader(kWelcome, header.PlayerId);
                    sendto(cSocket, reinterpret_cast<const char*>(&cWelcome), sizeof(cWelcome), 0, reinterpret_cast<const sockaddr*>(&from), sizeof(from));
                }
                else if (header.Type == kAudio && client != clients.end() && client->PlayerId == header.PlayerId)
                {
                    // Only from a client that said hello from this address, as the player it claimed.
                    client->LastSeen = cNow;
                    for (const auto& other : clients)
                    {
                        if (!SameAddress(other.Address, from))
                            sendto(cSocket, buffer, cSize, 0, reinterpret_cast<const sockaddr*>(&other.Address), sizeof(other.Address));
                    }
                }
                else if (header.Type == kBye && client != clients.end())
                {
                    Log("Voice relay: player %X left", client->PlayerId);
                    clients.erase(client);
                }
            }
        }

        const ULONGLONG cNow = GetTickCount64();
        if (cNow - lastPrune >= 1000)
        {
            lastPrune = cNow;
            for (auto it = clients.begin(); it != clients.end();)
            {
                if (cNow - it->LastSeen > kRelayTimeout)
                {
                    Log("Voice relay: player %X timed out", it->PlayerId);
                    it = clients.erase(it);
                }
                else
                    ++it;
            }
        }
    }

    closesocket(cSocket);
    s_relayClients = 0;
    s_relayTurnedAway = 0;
    s_relayRunning = false;
    return 0;
}

// Runs the relay in this game if the port is free. On one PC with two games (Nucleus Co-op), the second finds the
// port taken and simply uses the first one's relay.
void StartRelay() noexcept
{
    if (s_relayThread)
        return;

    SOCKET relay = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (relay == INVALID_SOCKET)
    {
        Log("Voice relay: could not create a socket (error %d)", WSAGetLastError());
        return;
    }
    BOOL exclusive = TRUE;
    setsockopt(relay, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(g_config.VoicePort);
    if (bind(relay, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        Log("Voice relay: UDP port %u is already in use (another game on this PC runs the relay?); using that relay", g_config.VoicePort);
        closesocket(relay);
        return;
    }

    s_relayStop = false;
    s_relayRunning = true;
    s_relayThread = CreateThread(nullptr, 0, &RelayThread, reinterpret_cast<LPVOID>(relay), 0, nullptr);
    if (!s_relayThread)
    {
        s_relayRunning = false;
        Log("Voice relay: could not start its thread");
        closesocket(relay);
        return;
    }
    s_relayPort = g_config.VoicePort;
    Log("Voice relay: running on UDP port %u. Players outside this network need this port forwarded to this PC, like "
        "Skyrim Together's own port.",
        g_config.VoicePort);
}

// Stops the relay (HostVoiceRelay switched off, the port changed, or the game left the server).
void StopRelay() noexcept
{
    if (!s_relayThread)
        return;
    s_relayStop = true;
    WaitForSingleObject(s_relayThread, 2000);
    CloseHandle(s_relayThread);
    s_relayThread = nullptr;
    s_relayStop = false;
    Log("Voice relay: stopped");
}

// --- speakers (other players) --------------------------------------------------------------------------------

struct Speaker
{
    explicit Speaker(uint32_t aId)
        : Id(aId)
        , Mouth(static_cast<float>(kRate), false)
    {
        int error = 0;
        Decoder = opus_decoder_create(kRate, 1, &error);
    }
    ~Speaker()
    {
        if (Decoder)
            opus_decoder_destroy(Decoder);
    }
    Speaker(const Speaker&) = delete;
    Speaker& operator=(const Speaker&) = delete;

    uint32_t Id;
    OpusDecoder* Decoder{};

    // Received frames by sequence, unwrapped so they keep counting past 65535.
    std::map<int64_t, std::vector<uint8_t>> Queue;
    bool HaveSequence{};
    int64_t Newest{};
    bool Playing{};
    int64_t Next{};

    std::vector<float> Pcm;
    size_t PcmPosition{};

    float Gain{};
    float Pan{};
    float Range{1.f};       // how far the voice carries, eased toward TargetRange
    float TargetRange{1.f}; // from the latest packet

    Analyzer Mouth;
    STRLipSync::Frame MouthFrame{};
    ULONGLONG LastAudio{};  // last time real (decoded) voice played
    ULONGLONG LastPacket{};

    uint32_t Received{};
    uint32_t Lost{};
    uint32_t Late{};
};

std::mutex s_speakersLock;
std::map<uint32_t, std::unique_ptr<Speaker>> s_speakers;

std::mutex s_mixLock;
std::map<uint32_t, STRLipSync::MixEntry> s_mix;

// Your own voice, decoded from the very packets the others get (HearOwnVoice), waiting to be played.
std::mutex s_selfLock;
std::vector<float> s_selfPcm;
size_t s_selfPosition = 0;
constexpr size_t kMaxSelfSamples = kRate / 5; // 200 ms; older is dropped so it stays close to real time

void OnAudioPacket(const Header& acHeader, const char* apPayload, int aPayloadSize) noexcept
{
    if (aPayloadSize <= 0)
        return;

    std::lock_guard guard(s_speakersLock);
    auto& slot = s_speakers[acHeader.PlayerId];
    if (!slot)
    {
        slot = std::make_unique<Speaker>(acHeader.PlayerId);
        if (!slot->Decoder)
        {
            s_speakers.erase(acHeader.PlayerId);
            return;
        }
        Log("Voice chat: hearing player %X", acHeader.PlayerId);
    }
    Speaker& speaker = *slot;
    speaker.LastPacket = GetTickCount64();
    ++speaker.Received;
    if (acHeader.Range)
        speaker.TargetRange = acHeader.Range / kRangeUnit;

    int64_t sequence = acHeader.Sequence;
    if (speaker.HaveSequence)
        sequence = speaker.Newest + static_cast<int16_t>(acHeader.Sequence - static_cast<uint16_t>(speaker.Newest));
    speaker.HaveSequence = true;
    speaker.Newest = std::max(speaker.Newest, sequence);

    if (speaker.Playing && sequence < speaker.Next)
    {
        ++speaker.Late;
        return;
    }
    if (speaker.Queue.size() < 64)
        speaker.Queue[sequence].assign(apPayload, apPayload + aPayloadSize);
}

// Decodes the next 20 ms of a speaker into its Pcm. Returns false when nothing is playing.
bool Refill(Speaker& aSpeaker) noexcept
{
    aSpeaker.Pcm.clear();
    aSpeaker.PcmPosition = 0;

    if (!aSpeaker.Playing)
    {
        if (aSpeaker.Queue.size() < kStartFrames)
            return false;
        aSpeaker.Playing = true;
        aSpeaker.Next = aSpeaker.Queue.begin()->first;
    }

    while (aSpeaker.Queue.size() > kMaxQueuedFrames)
    {
        aSpeaker.Queue.erase(aSpeaker.Queue.begin());
        aSpeaker.Next = aSpeaker.Queue.begin()->first;
    }

    if (aSpeaker.Queue.empty())
    {
        // The speaker stopped (or the network stalled): wait for two frames again.
        aSpeaker.Playing = false;
        return false;
    }

    float decoded[kMaxDecodeSamples];
    int samples = 0;
    auto it = aSpeaker.Queue.find(aSpeaker.Next);
    if (it != aSpeaker.Queue.end())
    {
        samples = opus_decode_float(aSpeaker.Decoder, it->second.data(), static_cast<opus_int32>(it->second.size()), decoded, kMaxDecodeSamples, 0);
        aSpeaker.Queue.erase(it);
    }
    else
    {
        // A frame is missing: rebuild it from the next frame's redundancy if that arrived, else let Opus conceal it.
        ++aSpeaker.Lost;
        auto following = aSpeaker.Queue.find(aSpeaker.Next + 1);
        if (following != aSpeaker.Queue.end())
            samples = opus_decode_float(aSpeaker.Decoder, following->second.data(), static_cast<opus_int32>(following->second.size()), decoded, kFrameSamples, 1);
        else
            samples = opus_decode_float(aSpeaker.Decoder, nullptr, 0, decoded, kFrameSamples, 0);
    }
    ++aSpeaker.Next;

    if (samples <= 0)
        return false;
    aSpeaker.Pcm.assign(decoded, decoded + samples);
    aSpeaker.LastAudio = GetTickCount64();
    return true;
}

// The volume of a voice heard aDistance away that carries aRange times the normal distances: full up to
// NearDistance, then falling off clearly (about -14 dB halfway to FarDistance) to silence at FarDistance.
float GainAt(float aDistance, float aRange) noexcept
{
    const float cNear = g_config.Shared.NearDistance * aRange;
    const float cFar = std::max(g_config.Shared.FarDistance * aRange, cNear + 1.f);
    const float cLeft = 1.f - Clamp01((aDistance - cNear) / (cFar - cNear));
    return cLeft * cLeft * cLeft;
}

// Mixes aCount stereo samples at 48 kHz from every speaker, at the distance and side the add-on set.
void MixVoices(float* apLeft, float* apRight, size_t aCount) noexcept
{
    std::fill(apLeft, apLeft + aCount, 0.f);
    std::fill(apRight, apRight + aCount, 0.f);

    std::map<uint32_t, STRLipSync::MixEntry> mix;
    {
        std::lock_guard guard(s_mixLock);
        mix = s_mix;
    }
    const bool cMuted = s_othersMuted.load();
    const float cVolume = g_config.VoiceVolume;
    const ULONGLONG cNow = GetTickCount64();

    // Your own voice, in the middle, at its own volume.
    {
        std::lock_guard guard(s_selfLock);
        const float cSelf = g_config.HearOwnVoice ? g_config.OwnVoiceVolume * 0.70710678f : 0.f;
        for (size_t i = 0; i < aCount && s_selfPosition < s_selfPcm.size(); ++i)
        {
            const float cSample = s_selfPcm[s_selfPosition++] * cSelf;
            apLeft[i] += cSample;
            apRight[i] += cSample;
        }
        if (s_selfPosition >= s_selfPcm.size())
        {
            s_selfPcm.clear();
            s_selfPosition = 0;
        }
    }

    std::lock_guard guard(s_speakersLock);
    for (auto it = s_speakers.begin(); it != s_speakers.end();)
    {
        Speaker& speaker = *it->second;
        if (cNow - speaker.LastPacket > kSpeakerExpiry)
        {
            Log("Voice chat: player %X has not been heard for a while", speaker.Id);
            it = s_speakers.erase(it);
            continue;
        }

        // A yell widens the range at once; a return to normal or a whisper eases in over a fraction of a second.
        const float cBlockSeconds = static_cast<float>(aCount) / kRate;
        const float cRangeTau = speaker.TargetRange > speaker.Range ? 0.05f : 0.3f;
        speaker.Range += (speaker.TargetRange - speaker.Range) * std::min(1.f, cBlockSeconds / cRangeTau);

        auto target = mix.find(speaker.Id);
        const float cTargetGain = (target == mix.end() || cMuted) ? 0.f : GainAt(target->second.Distance, speaker.Range) * cVolume;
        const float cTargetPan = target == mix.end() ? 0.f : std::clamp(target->second.Pan, -1.f, 1.f);
        // Glide to the new gain over this block, so moving around does not click.
        const float cGainStep = (cTargetGain - speaker.Gain) / static_cast<float>(aCount);
        const float cPanStep = (cTargetPan - speaker.Pan) / static_cast<float>(aCount);
        const bool cRecentlyHeard = cNow - speaker.LastAudio < kMouthActive + 500;

        for (size_t i = 0; i < aCount; ++i)
        {
            float sample = 0.f;
            if (speaker.PcmPosition < speaker.Pcm.size() || Refill(speaker))
                sample = speaker.Pcm[speaker.PcmPosition++];

            // The mouth follows the voice as it is heard (before volume, so distance does not shrink it).
            if (cRecentlyHeard && speaker.Mouth.Push(sample))
                speaker.MouthFrame = speaker.Mouth.Current();

            speaker.Gain += cGainStep;
            speaker.Pan += cPanStep;
            // Constant-power panning.
            const float cAngle = (speaker.Pan + 1.f) * 0.78539816f;
            apLeft[i] += sample * speaker.Gain * std::cos(cAngle);
            apRight[i] += sample * speaker.Gain * std::sin(cAngle);
        }
        speaker.Gain = cTargetGain;
        speaker.Pan = cTargetPan;
        ++it;
    }
}

// --- playback ------------------------------------------------------------------------------------------------

bool s_listedOutputs = false;
std::atomic<bool> s_restartPlayback{false};

bool RunPlayback(HANDLE aStop) noexcept
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))))
        return false;

    ComPtr<IMMDevice> device;
    std::wstring name;
    if (!PickDevice(enumerator.p, eRender, eConsole, GetOutputDeviceSetting(), "speakers", s_listedOutputs, &device, name))
    {
        Log("Voice chat: no speakers or headphones to play voices on; checking again in a few seconds");
        return false;
    }

    ComPtr<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))))
        return false;

    WAVEFORMATEX* pFormat = nullptr;
    if (FAILED(client->GetMixFormat(&pFormat)) || !pFormat)
        return false;
    const SampleFormat cFormat = DescribeFormat(pFormat);
    HRESULT hr = E_FAIL;
    if (cFormat.Usable && (cFormat.Float || cFormat.SampleBytes == 2))
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000 /* 20 ms */, 0, pFormat, nullptr);
    CoTaskMemFree(pFormat);
    if (FAILED(hr))
    {
        Log("Voice chat: could not play on %s (error 0x%08X)", ToUtf8(name.c_str()).c_str(), static_cast<unsigned>(hr));
        return false;
    }

    UINT32 bufferFrames = 0;
    HANDLE audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioRenderClient> render;
    if (!audioEvent || FAILED(client->SetEventHandle(audioEvent)) || FAILED(client->GetBufferSize(&bufferFrames)) ||
        FAILED(client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render))) || FAILED(client->Start()))
    {
        if (audioEvent)
            CloseHandle(audioEvent);
        Log("Voice chat: could not start playing on %s", ToUtf8(name.c_str()).c_str());
        return false;
    }
    Log("Voice chat: playing voices on %s (%u Hz, %u channel%s)", ToUtf8(name.c_str()).c_str(), cFormat.Rate, cFormat.Channels,
        cFormat.Channels == 1 ? "" : "s");

    // Voices are mixed at 48 kHz and brought to the device's rate.
    Resampler left, right;
    left.Reset(kRate, cFormat.Rate);
    right.Reset(kRate, cFormat.Rate);
    std::vector<float> mixLeft, mixRight, outLeft, outRight;

    const HANDLE cWaits[2] = {aStop, audioEvent};
    bool keep = true;
    for (;;)
    {
        const DWORD cWait = WaitForMultipleObjects(2, cWaits, FALSE, 200);
        if (cWait == WAIT_OBJECT_0)
        {
            keep = false;
            break;
        }
        if (s_restartPlayback.exchange(false))
        {
            Log("Voice chat: switching the playback device");
            hr = S_OK;
            break;
        }

        UINT32 padding = 0;
        if (FAILED(hr = client->GetCurrentPadding(&padding)))
            break;
        const UINT32 cFrames = bufferFrames - padding;
        if (cFrames == 0)
            continue;

        outLeft.clear();
        outRight.clear();
        while (outLeft.size() < cFrames)
        {
            const size_t cMissing = cFrames - outLeft.size();
            const size_t cInput = std::max<size_t>(1, static_cast<size_t>(std::ceil(cMissing * left.Step)));
            mixLeft.resize(cInput);
            mixRight.resize(cInput);
            MixVoices(mixLeft.data(), mixRight.data(), cInput);
            for (size_t i = 0; i < cInput; ++i)
            {
                left.Push(mixLeft[i], [&](float v) { outLeft.push_back(v); });
                right.Push(mixRight[i], [&](float v) { outRight.push_back(v); });
            }
        }

        BYTE* pData = nullptr;
        if (FAILED(hr = render->GetBuffer(cFrames, &pData)))
            break;
        for (UINT32 f = 0; f < cFrames; ++f)
        {
            const float cLeft = std::clamp(outLeft[f], -1.f, 1.f);
            const float cRight = std::clamp(outRight[f], -1.f, 1.f);
            for (uint32_t c = 0; c < cFormat.Channels; ++c)
            {
                const float cValue = cFormat.Channels == 1 ? 0.5f * (cLeft + cRight) : (c == 0 ? cLeft : (c == 1 ? cRight : 0.f));
                BYTE* p = pData + (static_cast<size_t>(f) * cFormat.Channels + c) * cFormat.SampleBytes;
                if (cFormat.Float)
                    std::memcpy(p, &cValue, 4);
                else
                {
                    const int16_t cPcm = static_cast<int16_t>(std::lround(cValue * 32767.f));
                    std::memcpy(p, &cPcm, 2);
                }
            }
        }
        render->ReleaseBuffer(cFrames, 0);
    }

    if (keep && FAILED(hr))
        Log("Voice chat: lost %s (error 0x%08X); looking for speakers again", ToUtf8(name.c_str()).c_str(), static_cast<unsigned>(hr));
    client->Stop();
    CloseHandle(audioEvent);
    return keep;
}

HANDLE s_stopEvent = nullptr;

DWORD WINAPI PlaybackThread(LPVOID)
{
    const HRESULT cCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;)
    {
        if (!RunPlayback(s_stopEvent))
        {
            if (WaitForSingleObject(s_stopEvent, 5000) == WAIT_OBJECT_0)
                break;
        }
        else if (WaitForSingleObject(s_stopEvent, 100) == WAIT_OBJECT_0)
            break;
    }
    if (SUCCEEDED(cCom))
        CoUninitialize();
    return 0;
}

// --- network -------------------------------------------------------------------------------------------------

void CloseSocket() noexcept
{
    std::lock_guard guard(s_sendLock);
    if (s_socket != INVALID_SOCKET)
    {
        if (s_connected)
        {
            const Header cBye = MakeHeader(kBye, s_localPlayer);
            sendto(s_socket, reinterpret_cast<const char*>(&cBye), sizeof(cBye), 0, reinterpret_cast<const sockaddr*>(&s_relayAddress), sizeof(s_relayAddress));
        }
        closesocket(s_socket);
    }
    s_socket = INVALID_SOCKET;
    s_connected = false;
}

bool Resolve(const std::string& acHost, uint16_t aPort, sockaddr_in& aOut) noexcept
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* pResult = nullptr;
    if (getaddrinfo(acHost.c_str(), nullptr, &hints, &pResult) != 0 || !pResult)
        return false;
    aOut = *reinterpret_cast<const sockaddr_in*>(pResult->ai_addr);
    aOut.sin_port = htons(aPort);
    freeaddrinfo(pResult);
    return true;
}

// Whether the server at acHost runs on this PC: a loopback address, or one of this PC's own addresses.
bool IsThisPc(const std::string& acHost) noexcept
{
    sockaddr_in server{};
    if (!Resolve(acHost, 0, server))
        return false;
    if ((ntohl(server.sin_addr.s_addr) >> 24) == 127)
        return true;

    char name[256]{};
    if (gethostname(name, sizeof(name)) != 0)
        return false;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* pResult = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &pResult) != 0 || !pResult)
        return false;
    bool found = false;
    for (const addrinfo* pEntry = pResult; pEntry && !found; pEntry = pEntry->ai_next)
        found = reinterpret_cast<const sockaddr_in*>(pEntry->ai_addr)->sin_addr.s_addr == server.sin_addr.s_addr;
    freeaddrinfo(pResult);
    return found;
}

DWORD WINAPI NetworkThread(LPVOID)
{
    uint64_t generation = 0;
    ULONGLONG lastHello = 0;
    ULONGLONG joinedAt = 0;
    bool warnedNoAnswer = false;
    bool loggedWelcome = false;
    bool muteKeyDown = false;
    ULONGLONG lastStats = GetTickCount64();
    char buffer[kMaxPacket];

    for (;;)
    {
        // Follow the session the add-on set.
        std::string host;
        uint32_t player = 0;
        uint64_t wanted = 0;
        {
            std::lock_guard guard(s_sessionLock);
            host = s_sessionHost;
            player = s_sessionPlayer;
            wanted = s_sessionGeneration;
        }
        if (wanted != generation)
        {
            generation = wanted;
            CloseSocket();
            {
                std::lock_guard guard(s_speakersLock);
                s_speakers.clear();
            }
            // The relay runs while this game hosts it and is on a server, on the port currently set. Auto hosts it
            // when the Skyrim Together server runs on this PC.
            const bool cHostRelay = !host.empty() && (g_config.HostVoiceRelay == 1 || (g_config.HostVoiceRelay == 2 && IsThisPc(host)));
            s_hostingWanted = cHostRelay;
            if (s_relayThread && (!cHostRelay || s_relayPort != g_config.VoicePort))
                StopRelay();
            if (cHostRelay)
                StartRelay();
            s_welcomed = false;

            if (!host.empty())
            {
                // The host reaches its own relay on this PC; everyone else on the Skyrim Together server's host.
                const std::string cRelayHost = cHostRelay ? "127.0.0.1" : host;
                sockaddr_in relay{};
                SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (sock == INVALID_SOCKET || !Resolve(cRelayHost, g_config.VoicePort, relay))
                {
                    Log("Voice chat: could not reach %s:%u", cRelayHost.c_str(), g_config.VoicePort);
                    if (sock != INVALID_SOCKET)
                        closesocket(sock);
                }
                else
                {
                    std::lock_guard guard(s_sendLock);
                    s_socket = sock;
                    s_relayAddress = relay;
                    s_localPlayer = player;
                    s_connected = true;
                    Log("Voice chat: joining the voice relay at %s as player %X", Describe(relay).c_str(), player);
                }
                lastHello = 0;
                joinedAt = GetTickCount64();
                warnedNoAnswer = false;
                loggedWelcome = false;
            }
            else
                Log("Voice chat: left the server");
        }

        SOCKET sock;
        sockaddr_in relay;
        {
            std::lock_guard guard(s_sendLock);
            sock = s_socket;
            relay = s_relayAddress;
        }

        const ULONGLONG cNow = GetTickCount64();

        // Hearing the others can be switched off with a key.
        if (g_config.MuteOthersKey)
        {
            const bool cDown = KeyDown(g_config.MuteOthersKey);
            if (cDown && !muteKeyDown)
            {
                const bool cMuted = !s_othersMuted.load();
                s_othersMuted = cMuted;
                Log("Voice chat: other players %s", cMuted ? "muted" : "unmuted");
            }
            muteKeyDown = cDown;
        }

        if (sock == INVALID_SOCKET)
        {
            Sleep(50);
            continue;
        }

        if (cNow - lastHello >= kHelloInterval)
        {
            lastHello = cNow;
            char hello[sizeof(Header) + kKeySize];
            const Header cHello = MakeHeader(kHello, player);
            std::memcpy(hello, &cHello, sizeof(cHello));
            std::memcpy(hello + sizeof(cHello), LocalKey().data(), kKeySize);
            sendto(sock, hello, sizeof(hello), 0, reinterpret_cast<const sockaddr*>(&relay), sizeof(relay));
        }
        if (!loggedWelcome && !warnedNoAnswer && cNow - joinedAt > kNoAnswerWarning)
        {
            warnedNoAnswer = true;
            Log("Voice chat: no answer from the voice relay at %s. The player hosting the Skyrim Together server needs voice chat on, "
                "HostVoiceRelay=Auto (or On) in LipSyncTogether.ini, and UDP port %u forwarded to their PC like Skyrim Together's own port.",
                Describe(relay).c_str(), g_config.VoicePort);
        }

        fd_set read;
        FD_ZERO(&read);
        FD_SET(sock, &read);
        timeval timeout{0, 20000};
        if (select(0, &read, nullptr, nullptr, &timeout) > 0)
        {
            sockaddr_in from{};
            int fromSize = sizeof(from);
            const int cSize = recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromSize);
            if (cSize > 0 && StartsWithMagic(buffer, cSize) && SameAddress(from, relay))
            {
                Header header;
                std::memcpy(&header, buffer, sizeof(header));
                if (header.Type == kWelcome && !loggedWelcome)
                {
                    loggedWelcome = true;
                    s_welcomed = true;
                    Log("Voice chat: connected to the voice relay; other players within earshot are heard");
                }
                else if (header.Type == kAudio && header.PlayerId != player)
                    OnAudioPacket(header, buffer + sizeof(Header), cSize - static_cast<int>(sizeof(Header)));
            }
        }

        if (g_config.LogLevels && cNow - lastStats >= 5000)
        {
            lastStats = cNow;
            std::lock_guard guard(s_speakersLock);
            for (auto& [id, speaker] : s_speakers)
            {
                Log("Voice chat: player %X: %u frames received, %u lost, %u late, %zu waiting, volume %.2f, range x%.2f", id, speaker->Received,
                    speaker->Lost, speaker->Late, speaker->Queue.size(), speaker->Gain, speaker->Range);
                speaker->Received = speaker->Lost = speaker->Late = 0;
            }
        }
    }
}

void StartThreads() noexcept
{
    if (s_threadsStarted.exchange(true))
        return;
    if (!EnsureWinsock())
        return;
    s_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE network = CreateThread(nullptr, 0, &NetworkThread, nullptr, 0, nullptr);
    HANDLE playback = CreateThread(nullptr, 0, &PlaybackThread, nullptr, 0, nullptr);
    if (!network || !playback)
        Log("Voice chat: could not start its threads");
    if (network)
    {
        SetThreadPriority(network, THREAD_PRIORITY_ABOVE_NORMAL);
        CloseHandle(network);
    }
    if (playback)
    {
        SetThreadPriority(playback, THREAD_PRIORITY_ABOVE_NORMAL);
        CloseHandle(playback);
    }
}

// --- sending (capture thread) --------------------------------------------------------------------------------

OpusEncoder* s_encoder = nullptr;
OpusDecoder* s_selfDecoder = nullptr;
Resampler s_micResampler;
uint32_t s_micRate = 0;
std::vector<float> s_micFrame;
std::vector<std::vector<float>> s_preroll; // the last two frames before the gate opened
bool s_wasSpeaking = false;
uint16_t s_sequence = 0;
float s_rangeScale = 1.f;

void SendFrame(const float* apFrame, bool aLast) noexcept
{
    unsigned char packet[kMaxPacket];
    const opus_int32 cBytes = opus_encode_float(s_encoder, apFrame, kFrameSamples, packet + sizeof(Header), static_cast<opus_int32>(kMaxPacket - sizeof(Header)));
    if (cBytes <= 0)
        return;

    // Hear yourself exactly as the others do: the same packet, decoded here, without the network in between.
    if (g_config.HearOwnVoice)
    {
        if (!s_selfDecoder)
        {
            int error = 0;
            s_selfDecoder = opus_decoder_create(kRate, 1, &error);
        }
        float decoded[kMaxDecodeSamples];
        const int cSamples = s_selfDecoder ? opus_decode_float(s_selfDecoder, packet + sizeof(Header), cBytes, decoded, kMaxDecodeSamples, 0) : 0;
        if (cSamples > 0)
        {
            std::lock_guard guard(s_selfLock);
            s_selfPcm.insert(s_selfPcm.end(), decoded, decoded + cSamples);
            if (s_selfPcm.size() - s_selfPosition > kMaxSelfSamples)
                s_selfPosition = s_selfPcm.size() - kMaxSelfSamples;
        }
    }

    std::lock_guard guard(s_sendLock);
    if (s_socket == INVALID_SOCKET || !s_connected)
        return;
    const Header cHeader = MakeHeader(kAudio, s_localPlayer, s_sequence++, aLast ? kFlagLast : 0, s_rangeScale);
    std::memcpy(packet, &cHeader, sizeof(cHeader));
    sendto(s_socket, reinterpret_cast<const char*>(packet), static_cast<int>(sizeof(Header) + cBytes), 0, reinterpret_cast<const sockaddr*>(&s_relayAddress),
           sizeof(s_relayAddress));
}

void OnFrame(bool aSpeaking) noexcept
{
    if (aSpeaking)
    {
        // The gate opens a few dozen milliseconds into a word; send what came just before, so it is not clipped.
        if (!s_wasSpeaking)
        {
            for (const auto& frame : s_preroll)
                SendFrame(frame.data(), false);
            s_preroll.clear();
        }
        SendFrame(s_micFrame.data(), false);
    }
    else if (s_wasSpeaking)
        SendFrame(s_micFrame.data(), true);
    else
    {
        s_preroll.push_back(s_micFrame);
        if (s_preroll.size() > 2)
            s_preroll.erase(s_preroll.begin());
    }
    s_wasSpeaking = aSpeaking;
}
} // namespace

void OnMicSamples(const float* apSamples, size_t aCount, uint32_t aRate, bool aSpeaking, float aRangeScale) noexcept
{
    if (!g_config.Shared.VoiceChat)
        return;
    s_rangeScale = aRangeScale;

    // With HearOwnVoice the voice is encoded even when not on a server, so it can be tried out alone.
    if (g_config.HearOwnVoice)
        StartThreads();
    {
        std::lock_guard guard(s_sendLock);
        if (!s_connected && !g_config.HearOwnVoice)
        {
            s_wasSpeaking = false;
            s_preroll.clear();
            s_micFrame.clear();
            return;
        }
    }

    if (!s_encoder)
    {
        int error = 0;
        // The full-fidelity application (not VOIP's narrower voice processing), at the highest quality setting.
        s_encoder = opus_encoder_create(kRate, 1, OPUS_APPLICATION_AUDIO, &error);
        if (!s_encoder)
        {
            Log("Voice chat: the Opus encoder could not start (error %d); your voice is not sent", error);
            g_config.Shared.VoiceChat = 0;
            return;
        }
        opus_encoder_ctl(s_encoder, OPUS_SET_BITRATE(static_cast<opus_int32>(g_config.VoiceBitrate)));
        opus_encoder_ctl(s_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(s_encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
        opus_encoder_ctl(s_encoder, OPUS_SET_COMPLEXITY(10));
        // Each frame also carries a rough copy of the one before, so one lost packet can be rebuilt.
        opus_encoder_ctl(s_encoder, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(s_encoder, OPUS_SET_PACKET_LOSS_PERC(5));
    }
    if (s_bitrateChanged.exchange(false))
        opus_encoder_ctl(s_encoder, OPUS_SET_BITRATE(static_cast<opus_int32>(g_config.VoiceBitrate)));
    if (aRate != s_micRate)
    {
        s_micRate = aRate;
        s_micResampler.Reset(aRate, kRate);
        s_micFrame.clear();
    }

    for (size_t i = 0; i < aCount; ++i)
    {
        s_micResampler.Push(apSamples[i], [&](float aSample) {
            s_micFrame.push_back(aSample);
            if (s_micFrame.size() == kFrameSamples)
            {
                OnFrame(aSpeaking);
                s_micFrame.clear();
            }
        });
    }
}

void SetSession(const char* acpServerHost, uint32_t aLocalPlayerId) noexcept
{
    const bool cJoining = acpServerHost && *acpServerHost;
    if (cJoining && !g_config.Shared.VoiceChat)
        return;
    if (cJoining)
        StartThreads();

    const std::string cHost = acpServerHost ? acpServerHost : "";
    {
        std::lock_guard guard(s_sessionLock);
        if (cHost == s_sessionHost && aLocalPlayerId == s_sessionPlayer)
            return;
        s_sessionHost = cHost;
        s_sessionPlayer = aLocalPlayerId;
        ++s_sessionGeneration;
    }

    // Never carry another server's whitelist into a new session.
    std::lock_guard keysGuard(s_keysLock);
    s_playerKeys.clear();
    if (cJoining)
        s_playerKeys[aLocalPlayerId] = LocalKey();
}

void SetMix(const STRLipSync::MixEntry* apEntries, uint32_t aCount) noexcept
{
    std::lock_guard guard(s_mixLock);
    s_mix.clear();
    for (uint32_t i = 0; apEntries && i < aCount && i < 256; ++i)
        s_mix[apEntries[i].PlayerId] = apEntries[i];
}

uint32_t GetRemoteMouth(uint32_t aPlayerId, STRLipSync::Frame* apFrame) noexcept
{
    if (!apFrame)
        return 0;
    std::lock_guard guard(s_speakersLock);
    auto it = s_speakers.find(aPlayerId);
    if (it == s_speakers.end() || GetTickCount64() - it->second->LastAudio > kMouthActive)
        return 0;
    *apFrame = it->second->MouthFrame;
    return 1;
}

uint32_t GetVoiceKey(char* apBuffer, uint32_t aSize) noexcept
{
    if (!apBuffer || aSize < kKeySize * 2 + 1)
        return 0;
    const VoiceKey& cKey = LocalKey();
    for (size_t i = 0; i < kKeySize; ++i)
        snprintf(apBuffer + i * 2, 3, "%02X", cKey[i]);
    return 1;
}

void AddVoiceKey(uint32_t aPlayerId, const char* acpKey) noexcept
{
    if (!acpKey || strlen(acpKey) != kKeySize * 2)
        return;
    VoiceKey key{};
    for (size_t i = 0; i < kKeySize; ++i)
    {
        const char cPair[3] = {acpKey[i * 2], acpKey[i * 2 + 1], 0};
        if (!isxdigit(static_cast<unsigned char>(cPair[0])) || !isxdigit(static_cast<unsigned char>(cPair[1])))
            return;
        key[i] = static_cast<uint8_t>(strtoul(cPair, nullptr, 16));
    }
    std::lock_guard guard(s_keysLock);
    s_playerKeys[aPlayerId] = key;
}

void RestartPlayback() noexcept
{
    s_restartPlayback = true;
}

void OnRelaySettingsChanged() noexcept
{
    // The network thread reconnects (and starts, stops or moves the relay) on a new session generation.
    std::lock_guard guard(s_sessionLock);
    if (!s_sessionHost.empty())
        ++s_sessionGeneration;
}

void OnBitrateChanged() noexcept
{
    s_bitrateChanged = true;
}

std::string DescribeStatus()
{
    if (!g_config.Shared.VoiceChat)
        return "Voice chat is off.";

    char line[256];
    std::string text;
    if (s_relayRunning)
    {
        snprintf(line, sizeof(line), "Hosting the voice relay on UDP port %u: %u player(s) connected.\n", s_relayPort, s_relayClients.load());
        text += line;
        if (g_config.RequireVoiceKey)
            snprintf(line, sizeof(line), "Only clients with a key from this Skyrim Together server can join (%u turned away).\n", s_relayTurnedAway.load());
        else
            snprintf(line, sizeof(line), "Open: anyone who knows this address and port can join.\n");
    }
    else if (s_hostingWanted)
        snprintf(line, sizeof(line), "Not hosting: not on a server yet, or another game on this PC already hosts port %u.\n", g_config.VoicePort);
    else
        snprintf(line, sizeof(line), "Using the relay on the Skyrim Together server's host.\n");
    text += line;

    {
        std::lock_guard guard(s_sendLock);
        if (!s_connected)
            snprintf(line, sizeof(line), "Not connected: join a Skyrim Together server.\n");
        else if (s_welcomed)
            snprintf(line, sizeof(line), "Connected to the relay at %s as player %X.\n", Describe(s_relayAddress).c_str(), s_localPlayer);
        else
            snprintf(line, sizeof(line), "Waiting for the relay at %s to answer...\n", Describe(s_relayAddress).c_str());
        text += line;
    }
    if (s_othersMuted)
        text += "The other players are muted (Mute others key).\n";

    std::lock_guard guard(s_speakersLock);
    if (s_speakers.empty())
        text += "Nobody heard yet.";
    for (const auto& [id, speaker] : s_speakers)
    {
        snprintf(line, sizeof(line), "Player %X: %s, volume %.0f%%, range x%.2f\n", id, speaker->Playing ? "talking" : "quiet", speaker->Gain * 100.f,
                 speaker->Range);
        text += line;
    }
    return text;
}
} // namespace LipSync::Voice
