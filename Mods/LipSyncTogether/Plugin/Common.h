#pragma once

// Shared by the parts of the LipSync Together plugin: the log, the settings and the Windows audio helpers.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>

#include "../StrAddon/Code/client/LipSyncTogether/STRLipSyncAPI.h"

#define LST_VERSION_MAJOR 0
#define LST_VERSION_MINOR 8
#define LST_VERSION_PATCH 0
#define LST_VERSION_STRING "0.8.0"

namespace LipSync
{
void Log(const char* acpFormat, ...);

std::string ToUtf8(const wchar_t* acpText);
std::wstring ToWide(const std::string& acText);
std::wstring Lower(std::wstring aText);

struct Config
{
    // Microphone and voice detection.
    std::string Device;          // part of the microphone's name; empty for the Windows default communications device
    float GateDb = 10.f;         // how far above the background noise counts as speech
    float MinLevelDb = -55.f;    // anything quieter never counts as speech
    float Sensitivity = 1.f;     // scales how far the jaw opens
    float HoldSeconds = 0.2f;    // the gate stays open this long after the voice drops, so words do not flutter
    bool NoiseFilter = true;     // only a voice (a sound with a pitch) opens the gate, not clicks or fans
    float VoiceThreshold = 0.5f; // how clearly pitched a sound must be to count as a voice (0 to 1)
    bool ShapesEnabled = true;   // false: jaw only
    uint32_t PushToTalkKey = 0;
    uint32_t MuteToggleKey = 0;
    bool LogLevels = false;

    // Voice chat.
    std::string OutputDevice;    // part of the speakers' or headphones' name; empty for the Windows default
    uint32_t HostVoiceRelay = 2; // this game runs the relay: 0 never, 1 always, 2 when the Skyrim Together server runs on this PC
    bool RequireVoiceKey = true; // the relay lets in only players whose voice key came through the Skyrim Together server
    uint16_t VoicePort = 10579;  // the relay's UDP port, on the Skyrim Together server's host
    float VoiceVolume = 1.f;     // scales every voice heard
    uint32_t VoiceBitrate = 64000; // full band (20 kHz) speech; 24000 sounds like a phone call
    bool HearOwnVoice = false;      // play your own voice back to you, as the others hear it
    float OwnVoiceVolume = 1.f;
    uint32_t MuteOthersKey = 0;  // toggles hearing the other players

    // Yelling and whispering change how far the voice carries, judged against the speaker's own normal voice.
    bool AutoRange = true;
    float YellRange = 2.5f;      // range multiplier when yelling
    float WhisperRange = 0.2f;   // range multiplier when whispering
    float YellDb = 10.f;         // this much louder than normal is a yell
    float WhisperDb = 8.f;       // this much quieter than normal is a whisper
    bool UnvoicedWhisper = true; // breathy whispers (no pitch) open the gate too, at whisper range

    STRLipSync::Settings Shared{1, 1.f, 0.04f, 4096.f, 0, 0, 400.f, 3000.f, 2.5f, 0x56, 1};

    std::wstring IniPath; // LipSyncTogether.ini next to the DLL
};

extern Config g_config;

// Guards the text settings (Device, OutputDevice), which the settings window can change while the audio threads
// read them; numbers are read and written whole.
extern std::mutex g_configLock;
std::string GetInputDeviceSetting();
std::string GetOutputDeviceSetting();

// Defined in LipSyncTogether.cpp: reopen the microphone (after the Device setting changed).
void RequestCaptureRestart() noexcept;

inline float Clamp01(float aValue) noexcept
{
    return std::clamp(aValue, 0.f, 1.f);
}

// Rises from 0 at aLow to 1 at aHigh.
inline float Ramp(float aValue, float aLow, float aHigh) noexcept
{
    return Clamp01((aValue - aLow) / (aHigh - aLow));
}

// Push-to-talk and the other keys only count while the game window has the focus.
bool GameHasFocus() noexcept;
bool KeyDown(uint32_t aVirtualKey) noexcept;

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

// A device's name as Windows shows it.
std::wstring DeviceName(IMMDevice* apDevice);

// The active audio device of aFlow whose name contains acWanted (any case), else the Windows default for aRole.
// Lists every device in the log the first time (aListed).
bool PickDevice(IMMDeviceEnumerator* apEnumerator, EDataFlow aFlow, ERole aRole, const std::string& acWanted, const char* acpKind, bool& aListed,
                IMMDevice** appDevice, std::wstring& aName);

// The shared-mode format of an audio client, read as mono float samples.
struct SampleFormat
{
    bool Float{};
    uint32_t Channels{};
    uint32_t SampleBytes{};
    uint32_t Rate{};
    bool Usable{};
};
SampleFormat DescribeFormat(const WAVEFORMATEX* apFormat) noexcept;
float ReadSample(const BYTE* apSample, const SampleFormat& acFormat) noexcept;
} // namespace LipSync
