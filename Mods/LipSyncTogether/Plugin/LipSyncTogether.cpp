// LipSync Together - moves Skyrim Together players' mouths while they talk.
//
// This plugin never touches game memory. It listens to the chosen microphone (WASAPI, shared mode, so voice chat
// programs keep working), gates out background noise, and turns the voice into five mouth shapes: jaw open from
// loudness, and lips spread, rounded, closed or teeth together from where the voice's energy sits in the spectrum.
// The LipSync Together add-on in the Skyrim Together launcher reads those shapes through the STR LipSync API, puts
// them on the player's face and relays them to the other players, so this one DLL works on every runtime SKSE
// supports (1.5.97, 1.6.x and 1.7.x).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <string>
#include <vector>

#include "../StrAddon/Code/client/LipSyncTogether/STRLipSyncAPI.h"

#define LST_VERSION_MAJOR 0
#define LST_VERSION_MINOR 1
#define LST_VERSION_PATCH 0
#define LST_VERSION_STRING "0.1.0"

namespace
{
// --- log -----------------------------------------------------------------------------------------------------

SRWLOCK s_logLock = SRWLOCK_INIT;

void Log(const char* acpFormat, ...)
{
    static std::wstring s_path;

    char line[1024];
    va_list args;
    va_start(args, acpFormat);
    vsnprintf(line, sizeof(line), acpFormat, args);
    va_end(args);

    AcquireSRWLockExclusive(&s_logLock);
    if (s_path.empty())
    {
        PWSTR pDocuments = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &pDocuments)))
        {
            std::wstring base = std::wstring(pDocuments) + L"\\My Games\\";
            CoTaskMemFree(pDocuments);

            // GOG installs keep their own folder.
            std::wstring folder = base + L"Skyrim Special Edition GOG\\SKSE";
            if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
                folder = base + L"Skyrim Special Edition\\SKSE";
            SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
            s_path = folder + L"\\LipSyncTogether.log";
            DeleteFileW(s_path.c_str());
        }
    }

    FILE* pFile = nullptr;
    if (!s_path.empty() && _wfopen_s(&pFile, s_path.c_str(), L"a") == 0 && pFile)
    {
        SYSTEMTIME time;
        GetLocalTime(&time);
        fprintf(pFile, "[%02d:%02d:%02d] %s\n", time.wHour, time.wMinute, time.wSecond, line);
        fclose(pFile);
    }
    ReleaseSRWLockExclusive(&s_logLock);
}

std::string ToUtf8(const wchar_t* acpText)
{
    if (!acpText || !*acpText)
        return {};
    const int cSize = WideCharToMultiByte(CP_UTF8, 0, acpText, -1, nullptr, 0, nullptr, nullptr);
    std::string out(cSize > 0 ? cSize - 1 : 0, '\0');
    if (cSize > 1)
        WideCharToMultiByte(CP_UTF8, 0, acpText, -1, out.data(), cSize, nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& acText)
{
    if (acText.empty())
        return {};
    const int cSize = MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, nullptr, 0);
    std::wstring out(cSize > 0 ? cSize - 1 : 0, L'\0');
    if (cSize > 1)
        MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, out.data(), cSize);
    return out;
}

std::wstring Lower(std::wstring aText)
{
    for (auto& c : aText)
        c = static_cast<wchar_t>(std::towlower(c));
    return aText;
}

// --- settings ------------------------------------------------------------------------------------------------

struct Config
{
    std::string Device;        // part of the microphone's name; empty for the Windows default communications device
    float GateDb = 10.f;       // how far above the background noise counts as speech
    float MinLevelDb = -55.f;  // anything quieter never counts as speech
    float Sensitivity = 1.f;   // scales how far the jaw opens
    float RangeDb = 24.f;      // loudness above the gate that opens the jaw fully
    float HoldSeconds = 0.15f; // the gate stays open this long after the voice drops, so words do not flutter
    bool ShapesEnabled = true; // false: jaw only
    uint32_t PushToTalkKey = 0;
    uint32_t MuteToggleKey = 0;
    bool LogLevels = false;

    STRLipSync::Settings Shared{1, 1.f, 0.1f, 4096.f};
};

Config s_config;

std::string Trim(const std::string& acText)
{
    const size_t cBegin = acText.find_first_not_of(" \t\r\n");
    if (cBegin == std::string::npos)
        return {};
    const size_t cEnd = acText.find_last_not_of(" \t\r\n");
    return acText.substr(cBegin, cEnd - cBegin + 1);
}

// LipSyncTogether.ini next to the DLL. Read with plain file I/O so Mod Organizer's virtual file system serves it.
void LoadConfig(HMODULE aModule)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(aModule, path, MAX_PATH);
    std::wstring iniPath(path);
    const size_t cDot = iniPath.find_last_of(L'.');
    if (cDot != std::wstring::npos)
        iniPath.resize(cDot);
    iniPath += L".ini";

    std::ifstream file(iniPath);
    if (!file)
    {
        Log("%s not found, using the default settings", ToUtf8(iniPath.c_str()).c_str());
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        const size_t cComment = line.find_first_of(";#");
        if (cComment != std::string::npos)
            line.resize(cComment);
        const size_t cEquals = line.find('=');
        if (cEquals == std::string::npos)
            continue;

        std::string key = Trim(line.substr(0, cEquals));
        const std::string cValue = Trim(line.substr(cEquals + 1));
        for (auto& c : key)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        const float cNumber = static_cast<float>(atof(cValue.c_str()));
        const uint32_t cInteger = static_cast<uint32_t>(strtoul(cValue.c_str(), nullptr, 0));

        if (key == "device")
            s_config.Device = cValue;
        else if (key == "gatedb")
            s_config.GateDb = std::clamp(cNumber, 0.f, 40.f);
        else if (key == "minleveldb")
            s_config.MinLevelDb = std::clamp(cNumber, -90.f, -10.f);
        else if (key == "sensitivity")
            s_config.Sensitivity = std::clamp(cNumber, 0.1f, 5.f);
        else if (key == "rangedb")
            s_config.RangeDb = std::clamp(cNumber, 6.f, 60.f);
        else if (key == "holdms")
            s_config.HoldSeconds = std::clamp(cNumber, 0.f, 2000.f) / 1000.f;
        else if (key == "mode")
            s_config.ShapesEnabled = _stricmp(cValue.c_str(), "jaw") != 0;
        else if (key == "pushtotalkkey")
            s_config.PushToTalkKey = cInteger & 0xFF;
        else if (key == "mutetogglekey")
            s_config.MuteToggleKey = cInteger & 0xFF;
        else if (key == "loglevels")
            s_config.LogLevels = cInteger != 0;
        else if (key == "animateself")
            s_config.Shared.AnimateSelf = cInteger != 0 ? 1 : 0;
        else if (key == "mouthstrength")
            s_config.Shared.MouthStrength = std::clamp(cNumber, 0.f, 2.f);
        else if (key == "remotedelayms")
            s_config.Shared.RemoteDelaySeconds = std::clamp(cNumber, 0.f, 1000.f) / 1000.f;
        else if (key == "maxdistance")
            s_config.Shared.MaxDistance = std::clamp(cNumber, 100.f, 100000.f);
    }

    Log("Settings: device '%s', gate %.1f dB over the noise (never below %.1f dB), sensitivity %.2f, hold %.0f ms, mode %s, "
        "push-to-talk key 0x%02X, mute toggle key 0x%02X, own face %s, mouth strength %.2f, remote delay %.0f ms, max distance %.0f",
        s_config.Device.empty() ? "(Windows default)" : s_config.Device.c_str(), s_config.GateDb, s_config.MinLevelDb, s_config.Sensitivity,
        s_config.HoldSeconds * 1000.f, s_config.ShapesEnabled ? "shapes" : "jaw", s_config.PushToTalkKey, s_config.MuteToggleKey,
        s_config.Shared.AnimateSelf ? "on" : "off", s_config.Shared.MouthStrength, s_config.Shared.RemoteDelaySeconds * 1000.f,
        s_config.Shared.MaxDistance);
}

// --- the published mouth -------------------------------------------------------------------------------------

SRWLOCK s_frameLock = SRWLOCK_INIT;
STRLipSync::Frame s_frame{};
bool s_haveFrame = false;
std::string s_deviceName;

void Publish(const STRLipSync::Frame& acFrame)
{
    AcquireSRWLockExclusive(&s_frameLock);
    s_frame = acFrame;
    s_haveFrame = true;
    ReleaseSRWLockExclusive(&s_frameLock);
}

void SetDeviceName(const std::string& acName)
{
    AcquireSRWLockExclusive(&s_frameLock);
    s_deviceName = acName;
    if (acName.empty())
        s_haveFrame = false;
    ReleaseSRWLockExclusive(&s_frameLock);
}

// --- analysis ------------------------------------------------------------------------------------------------

// Second-order IIR section (RBJ cookbook), transposed direct form II.
struct Biquad
{
    float b0{}, b1{}, b2{}, a1{}, a2{};
    float z1{}, z2{};
    bool Enabled{false};

    float Run(float aIn) noexcept
    {
        const float cOut = b0 * aIn + z1;
        z1 = b1 * aIn - a1 * cOut + z2;
        z2 = b2 * aIn - a2 * cOut;
        return cOut;
    }

    static Biquad BandPass(float aRate, float aCenter, float aQ) noexcept
    {
        Biquad f;
        if (aCenter >= aRate * 0.45f)
            return f;
        const float cW = 2.f * 3.14159265f * aCenter / aRate;
        const float cAlpha = std::sin(cW) / (2.f * aQ);
        const float cA0 = 1.f + cAlpha;
        f.b0 = cAlpha / cA0;
        f.b1 = 0.f;
        f.b2 = -cAlpha / cA0;
        f.a1 = -2.f * std::cos(cW) / cA0;
        f.a2 = (1.f - cAlpha) / cA0;
        f.Enabled = true;
        return f;
    }

    static Biquad HighPass(float aRate, float aCutoff) noexcept
    {
        Biquad f;
        const float cW = 2.f * 3.14159265f * aCutoff / aRate;
        const float cAlpha = std::sin(cW) / (2.f * 0.7071f);
        const float cCos = std::cos(cW);
        const float cA0 = 1.f + cAlpha;
        f.b0 = (1.f + cCos) / 2.f / cA0;
        f.b1 = -(1.f + cCos) / cA0;
        f.b2 = (1.f + cCos) / 2.f / cA0;
        f.a1 = -2.f * cCos / cA0;
        f.a2 = (1.f - cAlpha) / cA0;
        f.Enabled = true;
        return f;
    }
};

float Clamp01(float aValue) noexcept
{
    return std::clamp(aValue, 0.f, 1.f);
}

// Rises from 0 at aLow to 1 at aHigh.
float Ramp(float aValue, float aLow, float aHigh) noexcept
{
    return Clamp01((aValue - aLow) / (aHigh - aLow));
}

bool GameHasFocus() noexcept
{
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    return processId == GetCurrentProcessId();
}

// Turns mono samples into mouth shapes, one block (16 ms) at a time.
struct Analyzer
{
    static constexpr float kBlockSeconds = 0.016f;

    explicit Analyzer(float aRate)
        : m_rate(aRate)
        , m_blockSamples(std::max<uint32_t>(64, static_cast<uint32_t>(aRate * kBlockSeconds)))
    {
        m_highPass = Biquad::HighPass(aRate, 80.f);
        // First formant / back-vowel second formant, mid, front-vowel second formant, and fricative hiss.
        m_bands[0] = Biquad::BandPass(aRate, 550.f, 0.8f);
        m_bands[1] = Biquad::BandPass(aRate, 1350.f, 1.6f);
        m_bands[2] = Biquad::BandPass(aRate, 2500.f, 1.6f);
        m_bands[3] = Biquad::BandPass(aRate, 5500.f, 1.0f);
    }

    void Push(float aSample) noexcept
    {
        const float cHigh = m_highPass.Run(aSample);
        m_sumSquares += cHigh * cHigh;
        for (int b = 0; b < 4; ++b)
        {
            if (m_bands[b].Enabled)
            {
                const float cBand = m_bands[b].Run(cHigh);
                m_bandEnergy[b] += cBand * cBand;
            }
        }
        const bool cPositive = cHigh >= 0.f;
        if (cPositive != m_lastPositive && std::fabs(cHigh) > 1e-4f)
            ++m_crossings;
        m_lastPositive = cPositive;

        if (++m_count >= m_blockSamples)
            FinishBlock();
    }

    // No audio arrived for a while (device paused or gone): let the mouth close.
    void Silence(float aSeconds) noexcept
    {
        m_gateOpen = false;
        m_hold = 0.f;
        Smooth(STRLipSync::Frame{}, aSeconds);
        PublishCurrent(false);
    }

private:
    void FinishBlock() noexcept
    {
        const float cCount = static_cast<float>(m_count);
        const float cLevel = 10.f * std::log10(m_sumSquares / cCount + 1e-12f);
        const float cZeroCrossingRate = m_crossings / cCount * m_rate;

        float energy[4];
        float total = 1e-12f;
        for (int b = 0; b < 4; ++b)
        {
            energy[b] = m_bandEnergy[b];
            total += energy[b];
        }

        m_count = 0;
        m_crossings = 0;
        m_sumSquares = 0.f;
        std::fill(std::begin(m_bandEnergy), std::end(m_bandEnergy), 0.f);

        const float cDt = kBlockSeconds;

        // Background noise: follows a quieter room quickly, a louder one slowly (barely at all while speaking),
        // so a long sentence does not teach the gate that speech is noise.
        if (!m_floorReady)
        {
            m_floor = cLevel;
            m_floorReady = true;
        }
        else if (cLevel < m_floor)
            m_floor += (cLevel - m_floor) * std::min(1.f, cDt / 0.3f);
        else
            m_floor += (m_gateOpen ? 0.2f : 1.5f) * cDt;
        m_floor = std::clamp(m_floor, -100.f, -25.f);

        const float cThreshold = std::max(m_floor + s_config.GateDb, s_config.MinLevelDb);

        if (cLevel > cThreshold)
        {
            m_gateOpen = true;
            m_hold = s_config.HoldSeconds;
        }
        else if (cLevel < cThreshold - 3.f)
        {
            m_hold -= cDt;
            if (m_hold <= 0.f)
                m_gateOpen = false;
        }

        // Keys: push-to-talk and the mute toggle only count while the game has the focus.
        const bool cFocus = (s_config.PushToTalkKey || s_config.MuteToggleKey) && GameHasFocus();
        if (s_config.MuteToggleKey)
        {
            const bool cDown = cFocus && (GetAsyncKeyState(static_cast<int>(s_config.MuteToggleKey)) & 0x8000) != 0;
            if (cDown && !m_muteKeyDown)
            {
                m_muted = !m_muted;
                Log("Microphone %s", m_muted ? "muted (the mouth stays still)" : "unmuted");
            }
            m_muteKeyDown = cDown;
        }
        const bool cTalkKey = !s_config.PushToTalkKey || (cFocus && (GetAsyncKeyState(static_cast<int>(s_config.PushToTalkKey)) & 0x8000) != 0);
        const bool cSpeaking = m_gateOpen && cTalkKey && !m_muted;

        STRLipSync::Frame target{};
        if (cSpeaking)
        {
            const float cLoud = Clamp01(std::pow(Ramp(cLevel, cThreshold - 3.f, cThreshold + s_config.RangeDb), 0.7f) * s_config.Sensitivity);
            float open = cLoud;

            if (s_config.ShapesEnabled)
            {
                const float cLow = energy[0] / total;
                const float cFront = energy[2] / total;
                const float cHiss = energy[3] / total;

                // Hiss with many zero crossings: "s", "f", "sh" - teeth together, jaw nearly shut.
                const float cTeeth = Ramp(cHiss, 0.2f, 0.5f) * Ramp(cZeroCrossingRate, 1500.f, 3500.f);
                // Strong second formant up high: "ee", "i", "e" - lips spread.
                const float cWide = Ramp(cFront, 0.12f, 0.35f) * (1.f - cTeeth);
                // Nearly everything low with nothing up high: "oo", "o", "w" - lips rounded.
                const float cRound = Ramp(cLow, 0.6f, 0.85f) * (1.f - Ramp(cFront, 0.05f, 0.15f)) * (1.f - cTeeth);
                // A quiet hum that is almost all low: "m", "n", "b" - lips closed.
                const float cLips = Ramp(cLow, 0.8f, 0.95f) * (1.f - Ramp(cLoud, 0.15f, 0.4f));

                open *= (1.f - 0.8f * cLips) * (1.f - 0.5f * cTeeth);
                target.Wide = cWide;
                target.Round = cRound;
                target.Lips = cLips;
                target.Teeth = cTeeth;
            }
            target.Open = open;
        }

        Smooth(target, cDt);
        PublishCurrent(cSpeaking);

        if (s_config.LogLevels)
        {
            m_logTimer -= cDt;
            if (m_logTimer <= 0.f)
            {
                m_logTimer = 1.f;
                Log("level %.1f dB, noise %.1f dB, gate %.1f dB, %s | open %.2f wide %.2f round %.2f lips %.2f teeth %.2f | zcr %.0f/s",
                    cLevel, m_floor, cThreshold, cSpeaking ? "speaking" : (m_muted ? "muted" : (m_gateOpen ? "waiting for push-to-talk" : "quiet")),
                    m_current.Open, m_current.Wide, m_current.Round, m_current.Lips, m_current.Teeth, cZeroCrossingRate);
            }
        }
    }

    // Opens quickly, closes a little slower, so syllables read without jitter.
    void Smooth(const STRLipSync::Frame& acTarget, float aDt) noexcept
    {
        const float cAttack = 1.f - std::exp(-aDt / 0.025f);
        const float cRelease = 1.f - std::exp(-aDt / 0.07f);
        auto step = [&](float& aValue, float aTarget) {
            aValue += (aTarget - aValue) * (aTarget > aValue ? cAttack : cRelease);
            if (aValue < 0.005f)
                aValue = 0.f;
        };
        step(m_current.Open, acTarget.Open);
        step(m_current.Wide, acTarget.Wide);
        step(m_current.Round, acTarget.Round);
        step(m_current.Lips, acTarget.Lips);
        step(m_current.Teeth, acTarget.Teeth);
    }

    void PublishCurrent(bool aSpeaking) noexcept
    {
        m_current.Speaking = aSpeaking ? 1 : 0;
        ++m_current.Sequence;
        Publish(m_current);
    }

    float m_rate;
    uint32_t m_blockSamples;
    Biquad m_highPass;
    Biquad m_bands[4];

    uint32_t m_count{};
    uint32_t m_crossings{};
    bool m_lastPositive{};
    float m_sumSquares{};
    float m_bandEnergy[4]{};

    bool m_floorReady{};
    float m_floor{-60.f};
    bool m_gateOpen{};
    float m_hold{};
    bool m_muted{};
    bool m_muteKeyDown{};
    float m_logTimer{};

    STRLipSync::Frame m_current{};
};

// --- capture -------------------------------------------------------------------------------------------------

HANDLE s_stopEvent = nullptr;
bool s_listedDevices = false;

// PKEY_Device_FriendlyName.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

template <class T> struct ComPtr
{
    T* p = nullptr;
    ~ComPtr()
    {
        if (p)
            p->Release();
    }
    T** operator&() noexcept { return &p; }
    T* operator->() const noexcept { return p; }
    explicit operator bool() const noexcept { return p != nullptr; }
};

std::wstring FriendlyName(IMMDevice* apDevice)
{
    std::wstring name;
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(apDevice->OpenPropertyStore(STGM_READ, &store)))
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
            name = value.pwszVal;
        PropVariantClear(&value);
    }
    return name;
}

// The microphone named in the settings, else the Windows default communications microphone.
bool PickDevice(IMMDeviceEnumerator* apEnumerator, IMMDevice** appDevice, std::wstring& aName)
{
    ComPtr<IMMDeviceCollection> devices;
    UINT count = 0;
    if (SUCCEEDED(apEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices)))
        devices->GetCount(&count);

    const std::wstring cWanted = Lower(ToWide(s_config.Device));
    IMMDevice* pMatch = nullptr;
    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* pDevice = nullptr;
        if (FAILED(devices->Item(i, &pDevice)))
            continue;
        const std::wstring cName = FriendlyName(pDevice);
        if (!s_listedDevices)
            Log("  microphone %u: %s", i + 1, ToUtf8(cName.c_str()).c_str());
        if (!pMatch && !cWanted.empty() && Lower(cName).find(cWanted) != std::wstring::npos)
        {
            pMatch = pDevice;
            aName = cName;
            continue;
        }
        pDevice->Release();
    }
    if (!s_listedDevices)
    {
        s_listedDevices = true;
        if (count == 0)
            Log("  no microphone is connected");
    }

    if (pMatch)
    {
        *appDevice = pMatch;
        return true;
    }

    if (!cWanted.empty())
        Log("No microphone matches Device=%s; using the Windows default communications microphone", s_config.Device.c_str());

    if (FAILED(apEnumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, appDevice)) &&
        FAILED(apEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, appDevice)))
        return false;
    aName = FriendlyName(*appDevice);
    return true;
}

enum class CaptureEnd
{
    Stopped,
    Retry,     // the device went away or changed; try again soon
    Failed,    // could not open anything; try again later
};

CaptureEnd RunCapture()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))))
    {
        Log("Windows audio (MMDevice) is unavailable");
        return CaptureEnd::Failed;
    }

    ComPtr<IMMDevice> device;
    std::wstring name;
    if (!PickDevice(enumerator.p, &device, name))
    {
        Log("No microphone to listen to; checking again in a few seconds");
        return CaptureEnd::Failed;
    }

    ComPtr<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))))
    {
        Log("Could not open %s", ToUtf8(name.c_str()).c_str());
        return CaptureEnd::Failed;
    }

    WAVEFORMATEX* pFormat = nullptr;
    if (FAILED(client->GetMixFormat(&pFormat)) || !pFormat)
        return CaptureEnd::Failed;

    // The shared-mode format is nearly always 32-bit float; take integer PCM too.
    bool isFloat = pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool isPcm = pFormat->wFormatTag == WAVE_FORMAT_PCM;
    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pFormat->cbSize >= 22)
    {
        const auto* pExtensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pFormat);
        isFloat = pExtensible->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
        isPcm = pExtensible->SubFormat.Data1 == WAVE_FORMAT_PCM;
    }
    const uint32_t cChannels = std::max<uint32_t>(1, pFormat->nChannels);
    const uint32_t cSampleBytes = pFormat->nBlockAlign / cChannels;
    const float cRate = static_cast<float>(pFormat->nSamplesPerSec);
    const bool cUsable = (isFloat && cSampleBytes == 4) || (isPcm && (cSampleBytes == 2 || cSampleBytes == 3 || cSampleBytes == 4));

    HRESULT hr = E_FAIL;
    if (cUsable)
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000 /* 20 ms */, 0, pFormat, nullptr);
    const uint32_t cBits = pFormat->wBitsPerSample;
    CoTaskMemFree(pFormat);
    if (!cUsable || FAILED(hr))
    {
        Log("Could not listen to %s (format %s %u-bit, error 0x%08X)", ToUtf8(name.c_str()).c_str(), isFloat ? "float" : "integer", cBits,
            static_cast<unsigned>(hr));
        return CaptureEnd::Failed;
    }

    HANDLE audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioCaptureClient> capture;
    if (!audioEvent || FAILED(client->SetEventHandle(audioEvent)) ||
        FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture))) || FAILED(client->Start()))
    {
        if (audioEvent)
            CloseHandle(audioEvent);
        Log("Could not start listening to %s", ToUtf8(name.c_str()).c_str());
        return CaptureEnd::Failed;
    }

    const std::string cName = ToUtf8(name.c_str());
    Log("Listening to %s (%.0f Hz, %u channel%s)", cName.c_str(), cRate, cChannels, cChannels == 1 ? "" : "s");
    SetDeviceName(cName);

    Analyzer analyzer(cRate);
    CaptureEnd end = CaptureEnd::Stopped;
    ULONGLONG lastAudio = GetTickCount64();
    const HANDLE cWaits[2] = {s_stopEvent, audioEvent};

    for (;;)
    {
        const DWORD cWait = WaitForMultipleObjects(2, cWaits, FALSE, 100);
        if (cWait == WAIT_OBJECT_0)
            break;

        if (cWait == WAIT_TIMEOUT)
        {
            if (GetTickCount64() - lastAudio > 200)
                analyzer.Silence(0.1f);
            continue;
        }

        UINT32 packet = 0;
        while (SUCCEEDED(hr = capture->GetNextPacketSize(&packet)) && packet > 0)
        {
            BYTE* pData = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture->GetBuffer(&pData, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr))
                break;

            const bool cSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !pData;
            for (UINT32 f = 0; f < frames; ++f)
            {
                float sum = 0.f;
                if (!cSilent)
                {
                    const BYTE* pFrame = pData + static_cast<size_t>(f) * cChannels * cSampleBytes;
                    for (uint32_t c = 0; c < cChannels; ++c)
                    {
                        const BYTE* p = pFrame + c * cSampleBytes;
                        if (isFloat)
                        {
                            float v;
                            memcpy(&v, p, 4);
                            sum += v;
                        }
                        else if (cSampleBytes == 2)
                        {
                            int16_t v;
                            memcpy(&v, p, 2);
                            sum += v / 32768.f;
                        }
                        else if (cSampleBytes == 3)
                        {
                            const int32_t v = static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 8) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 24));
                            sum += v / 2147483648.f;
                        }
                        else
                        {
                            int32_t v;
                            memcpy(&v, p, 4);
                            sum += v / 2147483648.f;
                        }
                    }
                }
                analyzer.Push(sum / cChannels);
            }

            capture->ReleaseBuffer(frames);
            lastAudio = GetTickCount64();
        }

        if (FAILED(hr))
        {
            Log("Lost %s (error 0x%08X); looking for a microphone again", cName.c_str(), static_cast<unsigned>(hr));
            end = CaptureEnd::Retry;
            break;
        }
    }

    client->Stop();
    CloseHandle(audioEvent);
    SetDeviceName({});
    return end;
}

DWORD WINAPI CaptureThread(LPVOID)
{
    const HRESULT cCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;)
    {
        const CaptureEnd cEnd = RunCapture();
        if (cEnd == CaptureEnd::Stopped)
            break;
        if (WaitForSingleObject(s_stopEvent, cEnd == CaptureEnd::Retry ? 1000 : 5000) == WAIT_OBJECT_0)
            break;
    }
    if (SUCCEEDED(cCom))
        CoUninitialize();
    return 0;
}

// --- STR LipSync API -----------------------------------------------------------------------------------------

std::atomic<bool> s_captureStarted{false};

// Opens the microphone on its own thread, once.
void StartCapture()
{
    if (s_captureStarted.exchange(true))
        return;

    s_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = s_stopEvent ? CreateThread(nullptr, 0, &CaptureThread, nullptr, 0, nullptr) : nullptr;
    if (!thread)
    {
        Log("Could not start the microphone thread");
        return;
    }
    SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
    CloseHandle(thread);
}

// Only the add-on in the Skyrim Together launcher asks for frames, so the first request is what opens the
// microphone. (Looking for the launcher when SKSE loads the plugin does not work: once the launcher hosts the
// game, its own image is no longer among the process's listed modules.)
uint32_t GetFrame(STRLipSync::Frame* apFrame)
{
    if (!s_captureStarted.load())
    {
        Log("The Skyrim Together add-on asked for the mouth; opening the microphone");
        StartCapture();
    }

    if (!apFrame)
        return 0;
    AcquireSRWLockShared(&s_frameLock);
    const bool cHave = s_haveFrame;
    if (cHave)
        *apFrame = s_frame;
    ReleaseSRWLockShared(&s_frameLock);
    return cHave ? 1 : 0;
}

uint32_t GetDeviceName(char* apBuffer, uint32_t aSize)
{
    if (!apBuffer || aSize == 0)
        return 0;
    AcquireSRWLockShared(&s_frameLock);
    strncpy_s(apBuffer, aSize, s_deviceName.c_str(), _TRUNCATE);
    const bool cOpen = !s_deviceName.empty();
    ReleaseSRWLockShared(&s_frameLock);
    return cOpen ? 1 : 0;
}

const STRLipSync::Descriptor kDescriptor = {
    STRLipSync::kApiVersion, "LipSync Together", LST_VERSION_STRING, &s_config.Shared, &GetFrame, &GetDeviceName,
};

HMODULE s_module = nullptr;

void Start(uint32_t aRuntimeVersion)
{
    Log("LipSync Together %s loaded on runtime %u.%u.%u", LST_VERSION_STRING, (aRuntimeVersion >> 24) & 0xFF, (aRuntimeVersion >> 16) & 0xFF,
        (aRuntimeVersion >> 4) & 0xFFF);

    // The add-on reads these settings as soon as it finds the plugin.
    LoadConfig(s_module);
    Log("Waiting for the Skyrim Together add-on (the microphone stays closed if the game was not started with a "
        "SkyrimTogether.exe that carries it)");
}
} // namespace

extern "C" __declspec(dllexport) const STRLipSync::Descriptor* STRLipSync_GetDescriptor()
{
    return &kDescriptor;
}

// --- SKSE ----------------------------------------------------------------------------------------------------

namespace SKSE
{
// SKSE 2.0.x (1.5.97) query interface.
struct PluginInfo
{
    uint32_t infoVersion;
    const char* name;
    uint32_t version;
};

struct Interface
{
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
};

// SKSE 2.1+ (1.6.x and later) version data.
struct PluginVersionData
{
    uint32_t dataVersion;
    uint32_t pluginVersion;
    char name[256];
    char author[256];
    char supportEmail[252];
    uint32_t versionIndependenceEx;
    uint32_t versionIndependence;
    uint32_t compatibleVersions[16];
    uint32_t seVersionRequired;
};
static_assert(sizeof(PluginVersionData) == 0x350);

constexpr uint32_t kVersionIndependent_Signatures = 1 << 1;
constexpr uint32_t kVersionIndependent_StructsPost629 = 1 << 2;
constexpr uint32_t kVersionIndependentEx_NoStructUse = 1 << 0;
} // namespace SKSE

extern "C"
{
__declspec(dllexport) SKSE::PluginVersionData SKSEPlugin_Version = {
    1,
    (LST_VERSION_MAJOR << 24) | (LST_VERSION_MINOR << 16) | (LST_VERSION_PATCH << 4),
    "LipSyncTogether",
    "mailamea10",
    "",
    // No game addresses and no game structures are used, so any runtime is fine.
    SKSE::kVersionIndependentEx_NoStructUse,
    SKSE::kVersionIndependent_Signatures | SKSE::kVersionIndependent_StructsPost629,
    {0},
    0,
};

__declspec(dllexport) bool SKSEPlugin_Query(const SKSE::Interface* apSkse, SKSE::PluginInfo* apInfo)
{
    apInfo->infoVersion = 1;
    apInfo->name = "LipSyncTogether";
    apInfo->version = SKSEPlugin_Version.pluginVersion;
    return apSkse && !apSkse->isEditor;
}

__declspec(dllexport) bool SKSEPlugin_Load(const SKSE::Interface* apSkse)
{
    Start(apSkse ? apSkse->runtimeVersion : 0);
    return true;
}
}

BOOL APIENTRY DllMain(HMODULE aModule, DWORD aReason, LPVOID)
{
    if (aReason == DLL_PROCESS_ATTACH)
    {
        s_module = aModule;
        DisableThreadLibraryCalls(aModule);
    }
    return TRUE;
}
