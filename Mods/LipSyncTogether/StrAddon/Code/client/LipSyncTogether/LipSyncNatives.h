#pragma once

struct NativeFunction;

// Called from Skyrim Together's papyrus registration hook for every native the game registers (apply_addon.py
// adds the call). LipSync Together keeps the one it drives the face with: Mfg Fix's
// MfgConsoleFunc.SetPhonemeModifier. The function object itself is needed, not only the address the papyrus
// service records, because natives built on CommonLib keep their callback inside the object.
void LipSyncTogether_OnNativeRegistered(NativeFunction* apFunction) noexcept;

// Called from Skyrim Together's connect handler with the server it is about to dial ("host:port"; apply_addon.py
// adds the call). Voice chat reaches its relay on the same host.
void LipSyncTogether_OnConnect(const char* acpEndpoint) noexcept;

// Called from Skyrim Together's ImguiService::Render before each ImGui frame starts (apply_addon.py adds the call): the
// one moment fonts can be added. The settings window loads its fonts there the first time it opens.
void LipSyncTogether_BeforeImGuiFrame() noexcept;
