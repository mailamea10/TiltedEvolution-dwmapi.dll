// LipSync Together - moves Skyrim Together players' mouths while they talk, and optionally carries their voices.
//
// This plugin never touches game memory. It listens to the chosen microphone (WASAPI, shared mode, so voice chat
// programs keep working), gates out background noise, and turns the voice into five mouth shapes: jaw open from
// loudness, and lips spread, rounded, closed or teeth together from where the voice's energy sits in the spectrum.
// The LipSync Together add-on in the Skyrim Together launcher reads those shapes through the STR LipSync API, puts
// them on the player's face and relays them to the other players, so this one DLL works on every runtime SKSE
// supports (1.5.97, 1.6.x and 1.7.x). With VoiceChat=1 it also sends the voice itself and plays the other players'
// voices louder the closer they are (Voice.cpp).

#include "Analyzer.h"
#include "Common.h"
#include "Settings.h"
#include "Voice.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using namespace LipSync;

namespace
{
// --- settings ---------------------------------------------------------------------------------------------

// LipSyncTogether.ini next to the DLL, read with plain file I/O so Mod Organizer's virtual file system serves it.
void LoadConfig(HMODULE aModule)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(aModule, path, MAX_PATH);
    std::wstring iniPath(path);
    const size_t cDot = iniPath.find_last_of(L'.');
    if (cDot != std::wstring::npos)
        iniPath.resize(cDot);
    g_config.IniPath = iniPath + L".ini";

    Settings::Load();
    Settings::LogSummary();
    if (!g_config.Shared.VoiceChat)
        Log("Voice chat: off (VoiceChat=0); only mouths are shared");
    if (g_config.Shared.OpenKey)
        Log("Settings window: %s+key 0x%02X in game", g_config.Shared.OpenKeyShift ? "Shift" : "no modifier", g_config.Shared.OpenKey);
}

// --- the published mouth -------------------------------------------------------------------------------------

SRWLOCK s_frameLock = SRWLOCK_INIT;
STRLipSync::Frame s_frame{};
STRLipSync::Meter s_meter{};
bool s_haveFrame = false;
std::string s_deviceName;

void Publish(const Analyzer& acAnalyzer)
{
    AcquireSRWLockExclusive(&s_frameLock);
    s_frame = acAnalyzer.Current();
    s_meter = acAnalyzer.Meter();
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

// --- capture -------------------------------------------------------------------------------------------------

HANDLE s_stopEvent = nullptr;
bool s_listedMicrophones = false;
std::atomic<bool> s_restartCapture{false};

enum class CaptureEnd
{
    Stopped,
    Retry,  // the device went away or changed; try again soon
    Failed, // could not open anything; try again later
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
    if (!PickDevice(enumerator.p, eCapture, eCommunications, GetInputDeviceSetting(), "microphone", s_listedMicrophones, &device, name))
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
    const SampleFormat cFormat = DescribeFormat(pFormat);

    HRESULT hr = E_FAIL;
    if (cFormat.Usable)
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000 /* 20 ms */, 0, pFormat, nullptr);
    const uint32_t cBits = pFormat->wBitsPerSample;
    CoTaskMemFree(pFormat);
    if (!cFormat.Usable || FAILED(hr))
    {
        Log("Could not listen to %s (format %s %u-bit, error 0x%08X)", ToUtf8(name.c_str()).c_str(), cFormat.Float ? "float" : "integer", cBits,
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
    Log("Listening to %s (%u Hz, %u channel%s)", cName.c_str(), cFormat.Rate, cFormat.Channels, cFormat.Channels == 1 ? "" : "s");
    SetDeviceName(cName);

    Analyzer analyzer(static_cast<float>(cFormat.Rate), true);
    std::vector<float> mono;
    CaptureEnd end = CaptureEnd::Stopped;
    ULONGLONG lastAudio = GetTickCount64();
    const HANDLE cWaits[2] = {s_stopEvent, audioEvent};

    for (;;)
    {
        const DWORD cWait = WaitForMultipleObjects(2, cWaits, FALSE, 100);
        if (cWait == WAIT_OBJECT_0)
            break;
        if (s_restartCapture.exchange(false))
        {
            Log("Switching the microphone");
            end = CaptureEnd::Retry;
            break;
        }

        if (cWait == WAIT_TIMEOUT)
        {
            if (GetTickCount64() - lastAudio > 200)
            {
                analyzer.Silence(0.1f);
                Publish(analyzer);
            }
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
            mono.resize(frames);
            for (UINT32 f = 0; f < frames; ++f)
            {
                float sum = 0.f;
                if (!cSilent)
                {
                    const BYTE* pFrame = pData + static_cast<size_t>(f) * cFormat.Channels * cFormat.SampleBytes;
                    for (uint32_t c = 0; c < cFormat.Channels; ++c)
                        sum += ReadSample(pFrame + c * cFormat.SampleBytes, cFormat);
                }
                mono[f] = sum / cFormat.Channels;
                if (analyzer.Push(mono[f]))
                    Publish(analyzer);
            }
            capture->ReleaseBuffer(frames);
            lastAudio = GetTickCount64();

            Voice::OnMicSamples(mono.data(), mono.size(), cFormat.Rate, analyzer.Current().Speaking != 0, analyzer.RangeScale());
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
        if (WaitForSingleObject(s_stopEvent, cEnd == CaptureEnd::Retry ? 200 : 5000) == WAIT_OBJECT_0)
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

void SetSession(const char* acpServerHost, uint32_t aLocalPlayerId)
{
    Voice::SetSession(acpServerHost, aLocalPlayerId);
}

void SetMix(const STRLipSync::MixEntry* apEntries, uint32_t aCount)
{
    Voice::SetMix(apEntries, aCount);
}

uint32_t GetRemoteMouth(uint32_t aPlayerId, STRLipSync::Frame* apFrame)
{
    return Voice::GetRemoteMouth(aPlayerId, apFrame);
}

uint32_t CopyText(const std::string& acText, char* apBuffer, uint32_t aSize)
{
    if (!apBuffer || aSize == 0)
        return 0;
    strncpy_s(apBuffer, aSize, acText.c_str(), _TRUNCATE);
    return 1;
}

uint32_t GetSettingCount()
{
    return Settings::Count();
}

const STRLipSync::SettingInfo* GetSettingInfo(uint32_t aIndex)
{
    return Settings::Info(aIndex);
}

uint32_t GetSetting(uint32_t aIndex, char* apBuffer, uint32_t aSize)
{
    return aIndex < Settings::Count() ? CopyText(Settings::Get(aIndex), apBuffer, aSize) : 0;
}

uint32_t SetSetting(uint32_t aIndex, const char* acpValue)
{
    return acpValue && Settings::Set(aIndex, acpValue) ? 1 : 0;
}

uint32_t SaveSettings()
{
    return Settings::Save() ? 1 : 0;
}

uint32_t ReloadSettings()
{
    // Reading the INI again puts every value back; the device and relay follow if they differ.
    const std::string cDevice = GetInputDeviceSetting();
    const std::string cOutput = GetOutputDeviceSetting();
    const uint32_t cHost = g_config.HostVoiceRelay;
    const uint16_t cPort = g_config.VoicePort;
    const uint32_t cBitrate = g_config.VoiceBitrate;
    Settings::Load();
    Log("Settings reloaded from the INI");
    if (GetInputDeviceSetting() != cDevice)
        RequestCaptureRestart();
    if (GetOutputDeviceSetting() != cOutput)
        Voice::RestartPlayback();
    if (g_config.HostVoiceRelay != cHost || g_config.VoicePort != cPort)
        Voice::OnRelaySettingsChanged();
    if (g_config.VoiceBitrate != cBitrate)
        Voice::OnBitrateChanged();
    return 1;
}

uint32_t HasUnsavedChanges()
{
    return Settings::HasUnsavedChanges() ? 1 : 0;
}

uint32_t ListDevices(uint32_t aOutput, char* apBuffer, uint32_t aSize)
{
    return CopyText(Settings::ListDevices(aOutput != 0), apBuffer, aSize);
}

uint32_t GetMeter(STRLipSync::Meter* apMeter)
{
    if (!apMeter)
        return 0;
    AcquireSRWLockShared(&s_frameLock);
    const bool cHave = s_haveFrame;
    *apMeter = s_meter;
    ReleaseSRWLockShared(&s_frameLock);
    return cHave ? 1 : 0;
}

uint32_t GetVoiceStatus(char* apBuffer, uint32_t aSize)
{
    return CopyText(Voice::DescribeStatus(), apBuffer, aSize);
}

uint32_t GetVoiceKey(char* apBuffer, uint32_t aSize)
{
    return Voice::GetVoiceKey(apBuffer, aSize);
}

void AddVoiceKey(uint32_t aPlayerId, const char* acpKey)
{
    Voice::AddVoiceKey(aPlayerId, acpKey);
}

const STRLipSync::Descriptor kDescriptor = {
    STRLipSync::kApiVersion, "LipSync Together", LST_VERSION_STRING, &g_config.Shared, &GetFrame, &GetDeviceName, &SetSession, &SetMix, &GetRemoteMouth,
    &GetSettingCount, &GetSettingInfo, &GetSetting, &SetSetting, &SaveSettings, &ReloadSettings, &HasUnsavedChanges, &ListDevices, &GetMeter, &GetVoiceStatus,
    &GetVoiceKey, &AddVoiceKey,
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

void LipSync::RequestCaptureRestart() noexcept
{
    s_restartCapture = true;
}

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
