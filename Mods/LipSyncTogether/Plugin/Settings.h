#pragma once

#include "Common.h"

// Every setting of LipSyncTogether.ini in one table: how it is read, written and shown, and what changing it while the
// game runs sets in motion (reopening the microphone, restarting the relay, ...). Loading the INI, saving it and the
// in-game settings window all go through it.
namespace LipSync::Settings
{
void Load();       // reads g_config.IniPath
bool Save();       // rewrites the values in g_config.IniPath, keeping its comments
void LogSummary(); // what is in effect, for the log

uint32_t Count() noexcept;
const STRLipSync::SettingInfo* Info(uint32_t aIndex) noexcept;
std::string Get(uint32_t aIndex);
bool Set(uint32_t aIndex, const std::string& acValue); // from the settings window: takes effect now, logged
bool HasUnsavedChanges() noexcept;

// Names of the active microphones (aOutput false) or playback devices (aOutput true), one per line.
std::string ListDevices(bool aOutput);
} // namespace LipSync::Settings
