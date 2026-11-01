#pragma once

#include "Common.h"

// Proximity voice chat. Players' voices travel through a small UDP relay run by the game of the player who hosts the
// Skyrim Together server (HostVoiceRelay: Auto runs it when that server is on this PC), on that host's VoicePort. With
// RequireVoiceKey the relay lets in only players whose voice key came through the Skyrim Together server. Each game sends its microphone, Opus-encoded,
// only while its voice gate is open, and plays everyone else's at the volume and side the add-on asks for.
namespace LipSync::Voice
{
// Capture thread: mono microphone samples at aRate, whether the gate (and push-to-talk) lets the voice through, and
// how far it carries right now (Analyzer::RangeScale: above 1 yelling, below 1 whispering).
void OnMicSamples(const float* apSamples, size_t aCount, uint32_t aRate, bool aSpeaking, float aRangeScale) noexcept;

// STR LipSync API (see STRLipSyncAPI.h).
void SetSession(const char* acpServerHost, uint32_t aLocalPlayerId) noexcept;
void SetMix(const STRLipSync::MixEntry* apEntries, uint32_t aCount) noexcept;
uint32_t GetRemoteMouth(uint32_t aPlayerId, STRLipSync::Frame* apFrame) noexcept;
uint32_t GetVoiceKey(char* apBuffer, uint32_t aSize) noexcept;
void AddVoiceKey(uint32_t aPlayerId, const char* acpKey) noexcept;

// Settings changed while the game runs (Settings.cpp).
void RestartPlayback() noexcept;        // OutputDevice
void OnRelaySettingsChanged() noexcept; // HostVoiceRelay, VoicePort
void OnBitrateChanged() noexcept;       // VoiceBitrate

// A few lines on the relay, the connection and who is heard, for the settings window.
std::string DescribeStatus();
} // namespace LipSync::Voice
