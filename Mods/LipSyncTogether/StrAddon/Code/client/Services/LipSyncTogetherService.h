#pragma once

#include <LipSyncTogether/STRLipSyncAPI.h>
#include <Services/LipSyncTogetherWindow.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct NotifyScriptAnimation;
struct Actor;

/**
 * @brief The Skyrim Together side of LipSync Together (LipSyncTogether/STRLipSyncAPI.h).
 *
 * The LipSync Together SKSE plugin turns the microphone into a few mouth shapes. This service reads them every
 * frame, puts them on the player's own face, and relays them a few times a second through the script animation
 * relay every Skyrim Together server already has, so no server change is needed. On the other clients the shapes
 * are held for a short delay to even out the network, smoothed, and put on that player's copy; when the speaker
 * goes quiet the copy's mouth eases shut and is left alone again.
 *
 * Faces are moved with Mfg Fix's MfgConsoleFunc.SetPhonemeModifier (Mfg Fix NG on 1.6/1.7, Mfg Fix on 1.5.97),
 * called directly rather than through the papyrus virtual machine. Only phonemes are written, never expressions
 * or modifiers, and only the phonemes this service raised are put back to zero.
 *
 * The server keeps every distinct relayed string for as long as it runs, so each message is one of about a
 * hundred fixed mouth states per player rather than a stream of unique strings.
 *
 * With voice chat on, the plugin also carries the voices (through its own relay on the server's host, not through
 * Skyrim Together): this service tells it which server the game joined and, 30 times a second, how far away and on
 * which side each other player's character stands; the plugin sets the volume from that and from how far each voice
 * carries right now (yelling reaches further, whispering less far). Mouths of players whose voice is heard
 * follow that voice as it plays instead of the relayed states.
 *
 * Shift+V (OpenKey in the INI) opens the in-game settings window (LipSyncTogetherWindow), with Skyrim Together's
 * overlay around it for the cursor; every setting there takes effect at once.
 */
struct LipSyncTogetherService
{
    LipSyncTogetherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~LipSyncTogetherService() noexcept = default;

    TP_NOCOPYMOVE(LipSyncTogetherService);

    // The five mouth shapes, each 0 to 1 (see STRLipSync::Frame).
    struct Shape
    {
        float Open{};
        float Wide{};
        float Round{};
        float Lips{};
        float Teeth{};

        [[nodiscard]] bool IsClosed() const noexcept { return Open < 0.01f && Wide < 0.01f && Round < 0.01f && Lips < 0.01f && Teeth < 0.01f; }
    };

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept;

private:
    // The phonemes this service last wrote on one actor; -1 for one it has not touched.
    struct FaceState
    {
        uint32_t FormId{};
        int8_t Written[16];
        bool Owned{};

        FaceState() noexcept { Forget(); }
        void Forget() noexcept;
    };

    struct Received
    {
        double Time;
        Shape Mouth;
    };

    struct Remote
    {
        Vector<Received> Queue{};
        Shape Target{};
        Shape Current{};
        double LastReceived{};
        FaceState Face{};

        // Timing, logged once a second with LogLevels.
        uint32_t ReceivedCount{};
        double LargestGap{};
        String LastState{};
    };

    void ScanForPlugin() noexcept;
    bool ResolveFaceFunction() noexcept;
    void UpdateLocal(double aDelta, bool aApply) noexcept;
    void SendLocal(const Shape& acMouth, bool aSpeaking) noexcept;
    void UpdateRemotes(double aDelta, bool aApply) noexcept;
    void WriteFace(Actor* apActor, FaceState& aFace, const Shape& acMouth) noexcept;
    void ReleaseFace(Actor* apActor, FaceState& aFace) noexcept;
    void ReportTiming(double aDelta) noexcept;
    void UpdateVoice(bool aOnline, bool aMix) noexcept;
    void UpdateWindow() noexcept;
    void CloseWindow() noexcept;
    void Reset() noexcept;

    World& m_world;
    TransportService& m_transport;

    // The in-game settings window, and whether this service opened Skyrim Together's overlay for it.
    LipSyncTogetherWindow m_window;
    bool m_windowOpenedOverlay{};
    bool m_openKeyDown{};

    const void* m_pDescriptor{};
    double m_scanTimer{};
    double m_scanElapsed{};

    using TSetPhonemeModifier = bool (*)(void* apTag, Actor* apActor, int32_t aMode, uint32_t aId, int32_t aValue);
    TSetPhonemeModifier m_setPhonemeModifier{};
    bool m_faceFunctionChecked{};
    double m_faceFunctionWait{};

    double m_clock{};
    double m_applyTimer{};

    FaceState m_localFace{};
    String m_lastSentState{};
    double m_lastSendTime{-1.0};
    bool m_sentSpeaking{};
    bool m_wasOnline{};

    double m_reportTimer{};
    uint32_t m_sentCount{};
    double m_frameTimeSum{};
    double m_longestFrame{};
    uint32_t m_frameCount{};

    Map<uint32_t, Remote> m_remotes;
    Map<uint32_t, String> m_pendingVoiceKeys;

    // Voice chat: the session handed to the plugin, and the proximity mix sent to it.
    bool m_voiceSession{};
    String m_voiceHost{};
    uint32_t m_voicePlayer{};
    Vector<STRLipSync::MixEntry> m_mix{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_notifyConnection;
};
