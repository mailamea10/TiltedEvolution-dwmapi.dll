#pragma once

#include <LipSyncTogether/STRLipSyncAPI.h>

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

struct ImguiService;

/**
 * @brief LipSync Together's in-game settings window (Shift+V by default).
 *
 * Drawn with Skyrim Together's own ImGui, on the render thread, from the setting list the plugin publishes
 * (STRLipSyncAPI.h), so every INI setting appears without this window knowing them. Changes take effect at once
 * through the plugin; Save writes them to LipSyncTogether.ini. The service opens and closes Skyrim Together's overlay
 * around it (on the game thread), which is what shows the cursor and keeps the game from reacting to clicks.
 */
struct LipSyncTogetherWindow
{
    explicit LipSyncTogetherWindow(ImguiService& aImgui) noexcept;
    ~LipSyncTogetherWindow() noexcept = default;

    TP_NOCOPYMOVE(LipSyncTogetherWindow);

    void SetDescriptor(const STRLipSync::Descriptor* apDescriptor) noexcept { m_pDescriptor = apDescriptor; }

    // Set by the service on the game thread; read while drawing.
    std::atomic<bool> Visible{false};
    // Set while drawing when the window's close button or Close is pressed; the service closes it.
    std::atomic<bool> CloseRequested{false};
    // True while the window waits for a key to assign, so that key does not also toggle the window.
    std::atomic<bool> CapturingKey{false};
    // Set by the service when the window opens, so the device lists are read again.
    std::atomic<bool> RefreshDevices{true};

private:
    void Draw() noexcept;
    void DrawMeter(const STRLipSync::Descriptor& acDescriptor) noexcept;
    void DrawVoiceStatus(const STRLipSync::Descriptor& acDescriptor) noexcept;
    void DrawSetting(const STRLipSync::Descriptor& acDescriptor, uint32_t aIndex) noexcept;
    void DrawKey(const STRLipSync::Descriptor& acDescriptor, uint32_t aIndex, float aWidth, const char* acpValue) noexcept;
    void DrawDevice(const STRLipSync::Descriptor& acDescriptor, uint32_t aIndex, const STRLipSync::SettingInfo& acInfo, float aWidth, const char* acpValue) noexcept;
    void ReadDevices(const STRLipSync::Descriptor& acDescriptor) noexcept;

    std::atomic<const STRLipSync::Descriptor*> m_pDescriptor{nullptr};

    // The page shown (General, Voice chat, Hotkeys, Advanced).
    int m_page{0};

    // Values being dragged; applied when the slider is released.
    std::unordered_map<uint32_t, float> m_dragging;

    // Key capture.
    int m_captureIndex{-1};
    bool m_heldAtCapture[256]{};

    std::vector<std::string> m_inputDevices;
    std::vector<std::string> m_outputDevices;
    std::string m_status;

    entt::scoped_connection m_drawConnection;
};
