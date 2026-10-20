#include "Settings.h"

#include "Voice.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <vector>

namespace LipSync::Settings
{
namespace
{
struct Entry
{
    STRLipSync::SettingInfo Info;
    std::function<std::string()> Get;
    std::function<void(const std::string&)> Set; // stores the value (clamped); the caller applies side effects
    std::function<void()> Applied;               // what a change while the game runs sets in motion
};

std::vector<Entry> s_entries;
std::vector<bool> s_changed; // changed in game since the INI was read or saved
std::atomic<bool> s_unsaved{false};

std::string FormatFloat(float aValue)
{
    char text[32];
    snprintf(text, sizeof(text), "%g", aValue);
    return text;
}

std::string FormatKey(uint32_t aKey)
{
    char text[16];
    snprintf(text, sizeof(text), "0x%02X", aKey);
    return text;
}

float ToFloat(const std::string& acValue) { return static_cast<float>(atof(acValue.c_str())); }
uint32_t ToInteger(const std::string& acValue) { return static_cast<uint32_t>(strtoul(acValue.c_str(), nullptr, 0)); }

void AddBool(const char* acpSection, const char* acpKey, const char* acpLabel, const char* acpHelp, std::function<bool&()> aField,
             std::function<void()> aApplied = {})
{
    s_entries.push_back(Entry{{acpSection, acpKey, acpLabel, STRLipSync::kSettingBool, 0.f, 1.f, nullptr, acpHelp, "", "", ""},
                              [aField] { return std::string(aField() ? "1" : "0"); },
                              [aField](const std::string& v) { aField() = ToInteger(v) != 0; },
                              std::move(aApplied)});
}

// Flags shared with the add-on are uint32_t.
void AddFlag(const char* acpSection, const char* acpKey, const char* acpLabel, const char* acpHelp, uint32_t& aField, std::function<void()> aApplied = {})
{
    uint32_t* pField = &aField;
    s_entries.push_back(Entry{{acpSection, acpKey, acpLabel, STRLipSync::kSettingBool, 0.f, 1.f, nullptr, acpHelp, "", "", ""},
                              [pField] { return std::string(*pField ? "1" : "0"); },
                              [pField](const std::string& v) { *pField = ToInteger(v) != 0 ? 1 : 0; },
                              std::move(aApplied)});
}

void AddFloat(const char* acpSection, const char* acpKey, const char* acpLabel, const char* acpHelp, float& aField, float aMin, float aMax,
              std::function<void()> aApplied = {})
{
    float* pField = &aField;
    s_entries.push_back(Entry{{acpSection, acpKey, acpLabel, STRLipSync::kSettingFloat, aMin, aMax, nullptr, acpHelp, "", "", ""},
                              [pField] { return FormatFloat(*pField); },
                              [pField, aMin, aMax](const std::string& v) { *pField = std::clamp(ToFloat(v), aMin, aMax); },
                              std::move(aApplied)});
}

// A value kept in seconds but shown and stored in milliseconds.
void AddMilliseconds(const char* acpSection, const char* acpKey, const char* acpLabel, const char* acpHelp, float& aSeconds, float aMaxMs)
{
    float* pField = &aSeconds;
    s_entries.push_back(Entry{{acpSection, acpKey, acpLabel, STRLipSync::kSettingInt, 0.f, aMaxMs, nullptr, acpHelp, "", "", ""},
                              [pField] { return std::to_string(static_cast<int>(std::lround(*pField * 1000.f))); },
                              [pField, aMaxMs](const std::string& v) { *pField = std::clamp(ToFloat(v), 0.f, aMaxMs) / 1000.f; },
                              {}});
}

template <class T>
void AddInt(const char* acpSection, const char* acpKey, const char* acpLabel, const char* acpHelp, T& aField, uint32_t aMin, uint32_t aMax,
            std::function<void()> aApplied = {})
{
    T* pField = &aField;
    s_entries.push_back(Entry{{acpSection, acpKey, acpLabel, STRLipSync::kSettingInt, static_cast<float>(aMin), static_cast<float>(aMax), nullptr, acpHelp, "", "", ""},
                              [pField] { return std::to_string(static_cast<uint32_t>(*pField)); },
                              [pField, aMin, aMax](const std::string& v) { *pField = static_cast<T>(std::clamp(ToInteger(v), aMin, aMax)); },
                              std::move(aApplied)});
}

void AddKey(const char* acpSection, const char* acpKey, const char* acpLabel, const char* acpHelp, uint32_t& aField)
{
    uint32_t* pField = &aField;
    s_entries.push_back(Entry{{acpSection, acpKey, acpLabel, STRLipSync::kSettingKey, 0.f, 255.f, nullptr, acpHelp, "", "", ""},
                              [pField] { return FormatKey(*pField); },
                              [pField](const std::string& v) { *pField = ToInteger(v) & 0xFF; },
                              {}});
}

void AddDevice(const char* acpSection, const char* acpKey, const char* acpLabel, const char* acpHelp, std::string& aField, bool aOutput,
               std::function<void()> aApplied)
{
    std::string* pField = &aField;
    s_entries.push_back(Entry{{acpSection, acpKey, acpLabel, aOutput ? STRLipSync::kSettingOutputDevice : STRLipSync::kSettingInputDevice, 0.f, 0.f, nullptr,
                               acpHelp, "", "", ""},
                              [pField] {
                                  std::lock_guard guard(g_configLock);
                                  return *pField;
                              },
                              [pField](const std::string& v) {
                                  std::lock_guard guard(g_configLock);
                                  *pField = v;
                              },
                              std::move(aApplied)});
}

void Present();

void UpdateDerived()
{
    Config& c = g_config;
    c.Shared.FarDistance = std::max(c.Shared.FarDistance, c.Shared.NearDistance + 1.f);
    c.Shared.MaxRangeScale = c.AutoRange ? c.YellRange : 1.f;
    c.Shared.Debug = c.LogLevels ? 1 : 0;
}

void Build()
{
    if (!s_entries.empty())
        return;
    Config& c = g_config;
    s_entries.reserve(40);

    AddDevice("Microphone", "Device", "Microphone", "The microphone to listen to. Some virtual or software audio devices pass only silence.",
              c.Device, false, [] { RequestCaptureRestart(); });

    AddBool("Voice", "NoiseFilter", "Voice filter", "Only a voice (a sound with a pitch) moves the mouth; keyboard clicks, fans and room noise are ignored.",
            [] () -> bool& { return g_config.NoiseFilter; });
    AddFloat("Voice", "VoiceThreshold", "Voice threshold", "How clearly pitched a sound must be to count as a voice. Lower it if quiet speech is missed.",
             c.VoiceThreshold, 0.f, 1.f);
    AddFloat("Voice", "GateDb", "Gate (dB over noise)", "How far above the background noise counts as speech. Raise it if the mouth moves on noise.", c.GateDb, 0.f,
             40.f);
    AddFloat("Voice", "MinLevelDb", "Minimum level (dB)", "Anything quieter than this never counts as speech.", c.MinLevelDb, -90.f, -10.f);
    AddFloat("Voice", "Sensitivity", "Jaw sensitivity", "How far the jaw opens (1 = normal).", c.Sensitivity, 0.1f, 5.f);
    AddMilliseconds("Voice", "HoldMs", "Hold (ms)", "How long the mouth keeps moving after the voice stops (covers \"s\", \"t\" and short pauses).",
                    c.HoldSeconds, 2000.f);
    s_entries.push_back(Entry{{"Voice", "Mode", "Mouth shapes", STRLipSync::kSettingChoice, 0.f, 0.f, "Shapes|Jaw",
                               "Shapes: jaw plus lip shapes (spread, rounded, closed, teeth). Jaw: jaw only.", "", "", ""},
                              [] { return std::string(g_config.ShapesEnabled ? "Shapes" : "Jaw"); },
                              [](const std::string& v) { g_config.ShapesEnabled = _stricmp(v.c_str(), "jaw") != 0; },
                              {}});

    AddKey("Keys", "PushToTalkKey", "Push-to-talk key", "While set, the mouth (and voice) only goes out while this key is held.", c.PushToTalkKey);
    AddKey("Keys", "MuteToggleKey", "Mute key", "Press once to stop your mouth and voice, again to start them.", c.MuteToggleKey);

    AddFlag("Face", "AnimateSelf", "Move my own face", "Your own character's mouth moves too, not only the copies other players see.", c.Shared.AnimateSelf);
    AddFloat("Face", "MouthStrength", "Mouth strength", "Scales every mouth shape (1 = normal).", c.Shared.MouthStrength, 0.f, 2.f);

    AddMilliseconds("Network", "RemoteDelayMs", "Remote mouth delay (ms)", "How long other players' relayed mouths wait, to smooth out the network.",
                    c.Shared.RemoteDelaySeconds, 1000.f);
    AddFloat("Network", "MaxDistance", "Max mouth distance", "Players further away than this (game units) are left alone.", c.Shared.MaxDistance, 100.f,
             100000.f);

    AddFlag("VoiceChat", "VoiceChat", "Voice chat", "Carry your voice too, and hear the others louder the closer they are.", c.Shared.VoiceChat);
    // Older INIs say 0 or 1.
    s_entries.push_back(Entry{{"VoiceChat", "HostVoiceRelay", "Host the voice relay", STRLipSync::kSettingChoice, 0.f, 0.f, "Auto|On|Off",
                               "Auto: this game runs the relay when the Skyrim Together server runs on this PC. On: always. Off: never.", "", "", ""},
                              [] { return std::string(g_config.HostVoiceRelay == 0 ? "Off" : g_config.HostVoiceRelay == 1 ? "On" : "Auto"); },
                              [](const std::string& v) {
                                  g_config.HostVoiceRelay = _stricmp(v.c_str(), "off") == 0 || v == "0" ? 0 : _stricmp(v.c_str(), "on") == 0 || v == "1" ? 1 : 2;
                              },
                              [] { Voice::OnRelaySettingsChanged(); }});
    AddBool("VoiceChat", "RequireVoiceKey", "Require server key",
            "The relay accepts keys relayed through this Skyrim Together server. Keys remain until the server restarts. This does not encrypt audio. Off: anyone who can reach the relay can join.",
            [] () -> bool& { return g_config.RequireVoiceKey; });
    AddInt("VoiceChat", "VoicePort", "Relay port (UDP)", "The relay's UDP port on the server's host (Skyrim Together itself uses 10578).", c.VoicePort, 1024,
           65535, [] { Voice::OnRelaySettingsChanged(); });
    AddDevice("VoiceChat", "OutputDevice", "Play voices on", "Speakers or headphones for the other players' voices. Headphones stop echo.", c.OutputDevice,
              true, [] { Voice::RestartPlayback(); });
    AddFloat("VoiceChat", "VoiceVolume", "Voice volume", "Volume of the other players' voices (1 = normal).", c.VoiceVolume, 0.f, 4.f);
    AddFloat("VoiceChat", "NearDistance", "Full volume within", "Game units (400 is about 6 m).", c.Shared.NearDistance, 0.f, 100000.f);
    AddFloat("VoiceChat", "FarDistance", "Silent beyond", "Game units (3000 is about 43 m).", c.Shared.FarDistance, 1.f, 100000.f);
    AddInt("VoiceChat", "VoiceBitrate", "Voice quality (bit/s)", "64000 = full, natural voice (default); 24000 = phone-like, less bandwidth.",
           c.VoiceBitrate, 8000, 128000, [] { Voice::OnBitrateChanged(); });
    AddBool("VoiceChat", "HearOwnVoice", "Hear my own voice",
            "Plays your voice back to you exactly as the others hear it (after the voice filter and compression). The others hear you either way.",
            [] () -> bool& { return g_config.HearOwnVoice; });
    AddFloat("VoiceChat", "OwnVoiceVolume", "My voice volume", "How loud you hear yourself (1 = normal).", c.OwnVoiceVolume, 0.f, 2.f);
    AddKey("VoiceChat", "MuteOthersKey", "Mute others key", "Mutes and unmutes the other players for you.", c.MuteOthersKey);
    AddBool("VoiceChat", "AutoRange", "Automatic yell / whisper", "Speaking louder than your normal voice carries further, softer or whispering less far.",
            [] () -> bool& { return g_config.AutoRange; });
    AddFloat("VoiceChat", "YellRange", "Yell range (x)", "How far a yell carries, as a multiple of the distances above.", c.YellRange, 1.f, 10.f);
    AddFloat("VoiceChat", "WhisperRange", "Whisper range (x)", "How far a whisper carries, as a multiple of the distances above.", c.WhisperRange, 0.05f, 1.f);
    AddFloat("VoiceChat", "YellDb", "Yell at (+dB)", "How much louder than your normal voice counts as a full yell.", c.YellDb, 2.f, 30.f);
    AddFloat("VoiceChat", "WhisperDb", "Whisper at (-dB)", "How much quieter than your normal voice counts as a full whisper.", c.WhisperDb, 2.f, 30.f);
    AddBool("VoiceChat", "UnvoicedWhisper", "Breathy whispers", "Breathy whispers (no pitch) are sent too, at whisper range.",
            [] () -> bool& { return g_config.UnvoicedWhisper; });

    AddKey("UI", "OpenKey", "Settings window key", "Opens this window (together with Shift if the next option is on).", c.Shared.OpenKey);
    AddFlag("UI", "OpenKeyShift", "...with Shift", "The settings window key only counts together with Shift.", c.Shared.OpenKeyShift);

    AddBool("Debug", "LogLevels", "Log levels", "Writes the microphone level, mouth shapes and voice timing to the logs once a second.",
            [] () -> bool& { return g_config.LogLevels; });

    Present();
}

// How the settings window presents each setting, in display order: page, card, label, unit, help.
struct Presentation
{
    const char* Key;
    const char* Page;
    const char* Group;
    const char* Label;
    const char* Unit;
    const char* Help;
};

constexpr Presentation kPresentation[] = {
    {"Device", "General", "Microphone", "Microphone", "", "Pick your real microphone. Some virtual or software audio devices pass only silence."},
    {"NoiseFilter", "General", "Microphone", "Ignore keyboard and room noise", "", "Only your voice moves your mouth and is sent; clicks, fans and background noise are left out."},
    {"AnimateSelf", "General", "Your character", "Move my character's mouth", "", "Your own character talks too, not only the copy the others see."},
    {"MouthStrength", "General", "Your character", "Mouth movement", "x", "How wide mouths open (1 = normal)."},
    {"Mode", "General", "Your character", "Mouth shapes", "", "Shapes: jaw and lips follow your vowels. Jaw: the jaw only."},

    {"VoiceChat", "Voice chat", "Voice chat", "Voice chat", "", "Talk to the other players through the game. Off: only mouths are shared."},
    {"OutputDevice", "Voice chat", "Voice chat", "Play voices on", "", "Headphones are best: from speakers, voices can reach your microphone and echo."},
    {"VoiceVolume", "Voice chat", "Voice chat", "Other players' volume", "x", ""},
    {"HearOwnVoice", "Voice chat", "Voice chat", "Hear my own voice", "", "Hear yourself the way the others do. They hear you either way."},
    {"OwnVoiceVolume", "Voice chat", "Voice chat", "My voice volume", "x", ""},
    {"HostVoiceRelay", "Voice chat", "Hosting", "Host the voice relay", "",
     "Auto: on when the Skyrim Together server runs on this PC. Friends from outside need the relay port (Advanced) forwarded to that PC."},
    {"RequireVoiceKey", "Voice chat", "Hosting", "Require server key", "",
     "Only clients with a key relayed through this server can join; keys remain valid until it restarts. Audio is not encrypted. Off: anyone can."},
    {"NearDistance", "Voice chat", "Proximity", "Full volume within", "units", ""},
    {"FarDistance", "Voice chat", "Proximity", "Silent beyond", "units", ""},
    {"AutoRange", "Voice chat", "Proximity", "Yell and whisper", "", "Speak up to be heard further away; speak softly or whisper to be heard only nearby."},

    {"PushToTalkKey", "Hotkeys", "Talking", "Push to talk", "", "While set, you are only heard (and your mouth moves) while you hold it."},
    {"MuteToggleKey", "Hotkeys", "Talking", "Mute my microphone", "", ""},
    {"MuteOthersKey", "Hotkeys", "Talking", "Mute the other players", "", ""},
    {"OpenKey", "Hotkeys", "This window", "Open this window", "", ""},
    {"OpenKeyShift", "Hotkeys", "This window", "...together with Shift", "", ""},

    {"VoiceThreshold", "Advanced", "Voice detection", "Voice threshold", "", "How clearly pitched a sound must be to count as a voice. Lower it if soft speech is missed."},
    {"GateDb", "Advanced", "Voice detection", "Gate above the noise", "dB", "Raise it if noise still gets through; lower it if quiet speech is missed."},
    {"MinLevelDb", "Advanced", "Voice detection", "Minimum level", "dB", ""},
    {"HoldMs", "Advanced", "Voice detection", "Keep talking for", "ms", "After your voice stops, so short pauses and \"s\" sounds are not cut off."},
    {"Sensitivity", "Advanced", "Voice detection", "Jaw sensitivity", "x", ""},
    {"UnvoicedWhisper", "Advanced", "Voice detection", "Detect breathy whispers", "", ""},
    {"YellRange", "Advanced", "Yell and whisper", "Yell carries", "x", ""},
    {"WhisperRange", "Advanced", "Yell and whisper", "Whisper carries", "x", ""},
    {"YellDb", "Advanced", "Yell and whisper", "Yell when louder by", "dB", ""},
    {"WhisperDb", "Advanced", "Yell and whisper", "Whisper when softer by", "dB", ""},
    {"VoiceBitrate", "Advanced", "Network", "Voice quality", "bit/s", "64 kbit/s sounds natural; lower uses less bandwidth."},
    {"VoicePort", "Advanced", "Network", "Relay port (UDP)", "", "Skyrim Together itself uses 10578."},
    {"RemoteDelayMs", "Advanced", "Network", "Mouth delay for others", "ms", "Only for players without voice chat."},
    {"MaxDistance", "Advanced", "Network", "Mouths move within", "units", ""},
    {"LogLevels", "Advanced", "Debug", "Write levels to the log", "", "LipSyncTogether.log gets your level and voice timing once a second."},
};

// Gives every setting its page, card, label, unit and help, and puts them in display order.
void Present()
{
    std::vector<Entry> ordered;
    ordered.reserve(s_entries.size());
    for (const auto& presentation : kPresentation)
    {
        auto it = std::find_if(s_entries.begin(), s_entries.end(), [&](const Entry& e) { return _stricmp(e.Info.Key, presentation.Key) == 0; });
        if (it == s_entries.end())
            continue;
        Entry entry = std::move(*it);
        s_entries.erase(it);
        entry.Info.Page = presentation.Page;
        entry.Info.Group = presentation.Group;
        entry.Info.Label = presentation.Label;
        entry.Info.Unit = presentation.Unit;
        entry.Info.Help = presentation.Help;
        ordered.push_back(std::move(entry));
    }
    // Anything not listed above still shows, on the Advanced page.
    for (auto& entry : s_entries)
    {
        entry.Info.Page = "Advanced";
        entry.Info.Group = "Other";
        entry.Info.Unit = "";
        ordered.push_back(std::move(entry));
    }
    s_entries = std::move(ordered);
}

std::string Trim(const std::string& acText)
{
    const size_t cBegin = acText.find_first_not_of(" \t\r\n");
    if (cBegin == std::string::npos)
        return {};
    const size_t cEnd = acText.find_last_not_of(" \t\r\n");
    return acText.substr(cBegin, cEnd - cBegin + 1);
}

Entry* Find(const std::string& acKey)
{
    for (auto& entry : s_entries)
    {
        if (_stricmp(entry.Info.Key, acKey.c_str()) == 0)
            return &entry;
    }
    return nullptr;
}

// "Key=value" with any comment cut off, or false.
bool ParseLine(const std::string& acLine, std::string& aKey, std::string& aValue)
{
    std::string line = acLine;
    const size_t cComment = line.find_first_of(";#");
    if (cComment != std::string::npos)
        line.resize(cComment);
    const size_t cEquals = line.find('=');
    if (cEquals == std::string::npos)
        return false;
    aKey = Trim(line.substr(0, cEquals));
    aValue = Trim(line.substr(cEquals + 1));
    return !aKey.empty();
}
} // namespace

void Load()
{
    Build();
    std::ifstream file(g_config.IniPath);
    if (!file)
    {
        Log("%s not found, using the default settings", ToUtf8(g_config.IniPath.c_str()).c_str());
        UpdateDerived();
        return;
    }

    std::string line, key, value;
    while (std::getline(file, line))
    {
        if (!ParseLine(line, key, value))
            continue;
        if (Entry* pEntry = Find(key))
            pEntry->Set(value);
    }
    UpdateDerived();
    s_changed.assign(s_entries.size(), false);
    s_unsaved = false;
}

bool Save()
{
    Build();
    std::vector<std::string> lines;
    {
        std::ifstream file(g_config.IniPath);
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
    }

    // Rewrite only the values changed in game, in place, so the comments and the way the rest is written stay; add
    // settings the file does not have yet to the end of their section.
    s_changed.resize(s_entries.size(), false);
    std::vector<bool> written(s_entries.size(), false);
    std::string key, value;
    for (auto& line : lines)
    {
        if (!ParseLine(line, key, value))
            continue;
        for (size_t i = 0; i < s_entries.size(); ++i)
        {
            if (_stricmp(s_entries[i].Info.Key, key.c_str()) == 0)
            {
                if (s_changed[i])
                    line = std::string(s_entries[i].Info.Key) + "=" + s_entries[i].Get();
                written[i] = true;
                break;
            }
        }
    }
    for (size_t i = 0; i < s_entries.size(); ++i)
    {
        if (written[i])
            continue;
        const std::string cHeader = std::string("[") + s_entries[i].Info.Section + "]";
        const std::string cLine = std::string(s_entries[i].Info.Key) + "=" + s_entries[i].Get();
        auto header = std::find_if(lines.begin(), lines.end(), [&](const std::string& l) { return _stricmp(Trim(l).c_str(), cHeader.c_str()) == 0; });
        if (header == lines.end())
        {
            lines.push_back("");
            lines.push_back(cHeader);
            lines.push_back(cLine);
            continue;
        }
        // After the section's last key line.
        auto insert = header + 1;
        for (auto it = header + 1; it != lines.end() && Trim(*it).rfind('[', 0) != 0; ++it)
        {
            if (ParseLine(*it, key, value))
                insert = it + 1;
        }
        lines.insert(insert, cLine);
    }

    std::ofstream out(g_config.IniPath, std::ios::trunc);
    if (!out)
    {
        Log("Could not write %s", ToUtf8(g_config.IniPath.c_str()).c_str());
        return false;
    }
    for (const auto& line : lines)
        out << line << "\n";
    s_changed.assign(s_entries.size(), false);
    s_unsaved = false;
    Log("Settings saved to %s", ToUtf8(g_config.IniPath.c_str()).c_str());
    return true;
}

void LogSummary()
{
    Build();
    std::string line;
    const char* pSection = "";
    for (const auto& entry : s_entries)
    {
        if (strcmp(pSection, entry.Info.Section) != 0)
        {
            if (!line.empty())
                Log("%s", line.c_str());
            pSection = entry.Info.Section;
            line = std::string("Settings [") + pSection + "]:";
        }
        std::string value = entry.Get();
        if (value.empty())
            value = "(Windows default)";
        line += std::string(" ") + entry.Info.Key + "=" + value;
    }
    if (!line.empty())
        Log("%s", line.c_str());
}

uint32_t Count() noexcept
{
    Build();
    return static_cast<uint32_t>(s_entries.size());
}

const STRLipSync::SettingInfo* Info(uint32_t aIndex) noexcept
{
    Build();
    return aIndex < s_entries.size() ? &s_entries[aIndex].Info : nullptr;
}

std::string Get(uint32_t aIndex)
{
    Build();
    return aIndex < s_entries.size() ? s_entries[aIndex].Get() : std::string{};
}

bool Set(uint32_t aIndex, const std::string& acValue)
{
    Build();
    if (aIndex >= s_entries.size())
        return false;
    Entry& entry = s_entries[aIndex];
    const std::string cBefore = entry.Get();
    entry.Set(acValue);
    UpdateDerived();
    const std::string cAfter = entry.Get();
    if (cAfter == cBefore)
        return true;

    s_changed.resize(s_entries.size(), false);
    s_changed[aIndex] = true;
    s_unsaved = true;
    Log("Setting changed in game: [%s] %s=%s", entry.Info.Section, entry.Info.Key, cAfter.empty() ? "(Windows default)" : cAfter.c_str());
    if (entry.Applied)
        entry.Applied();
    return true;
}

bool HasUnsavedChanges() noexcept
{
    return s_unsaved.load();
}

std::string ListDevices(bool aOutput)
{
    // Callable from any thread: COM is set up here if the caller has not.
    const HRESULT cCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string names;
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))) &&
            SUCCEEDED(enumerator->EnumAudioEndpoints(aOutput ? eRender : eCapture, DEVICE_STATE_ACTIVE, &devices)))
            devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            if (FAILED(devices->Item(i, &device)))
                continue;
            names += ToUtf8(DeviceName(device.p).c_str());
            names += "\n";
        }
    }
    if (SUCCEEDED(cCom))
        CoUninitialize();
    return names;
}
} // namespace LipSync::Settings
