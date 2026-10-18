#pragma once

struct NativeFunction;

// Called from Skyrim Together's papyrus registration hook for every native the game registers (apply_addon.py
// adds the call). LipSync Together keeps the one it drives the face with: Mfg Fix's
// MfgConsoleFunc.SetPhonemeModifier. The function object itself is needed, not only the address the papyrus
// service records, because natives built on CommonLib keep their callback inside the object.
void LipSyncTogether_OnNativeRegistered(NativeFunction* apFunction) noexcept;
