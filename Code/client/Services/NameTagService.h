#pragma once

#include <unordered_map>
#include <string>

#include <imgui.h>

#include <Events/UpdateEvent.h>
#include <ChatMessageTypes.h>

struct World;
struct NotifyChatMessageBroadcast;
struct PlayerCharacter;
struct Actor;
struct NiPoint3;
struct ImguiService;

/**
 * @brief Draws 3D-aware name tags above other players' heads.
 *
 * Plain name text (a crown marks the party leader), scaled with distance.
 * Which tags show, and how, is set by the "Player UI filters" settings in the
 * overlay (see SetSettings). A player's floating chat line replaces their tag
 * while it shows.
 */
struct NameTagService
{
    // Matches the order of PlayerUiFilter in the UI.
    enum class Filter : uint8_t
    {
        kOn = 0,
        kParty = 1,
        kOff = 2,
    };

    struct Settings
    {
        Filter NameTags = Filter::kParty;
        Filter Backdrop = Filter::kParty;
        // Party: party members' chat and dialogue choices float above them. On: strangers' chat too.
        Filter FloatingChat = Filter::kParty;
        float FontSize = 16.f;
        float NameFadeStart = 3000.f;
        float NameInvisible = 5000.f;
        float PartyFadeStart = 3000.f;
        float PartyInvisible = 5000.f;
        // Drawn in italics instead of the name of players whose name is hidden.
        String StrangerLabel = "Stranger";
        // Players whose real name may be shown. The UI decides this from the
        // stranger name setting, party membership and the per-name eye.
        Set<uint32_t> RevealedPlayerIds;
        // Players whose name may be shown in front of their chat (known chat names setting and the eye).
        Set<uint32_t> RevealedChatPlayerIds;
    };

    NameTagService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~NameTagService() noexcept = default;

    TP_NOCOPYMOVE(NameTagService);

    void SetSettings(Settings aSettings) noexcept;

    // Tunables (kept few on purpose). Pixel values are at 1080p and scale with the screen height.
    static constexpr float kNearBypassDistance = 600.f;
    // The tag anchors to the neck bone, which stays on the body when dismemberment mods remove the head.
    // World units from the neck bone to the top of the head, for a 128-unit tall actor.
    static constexpr float kHeadTopAboveNeck = 24.f;
    // Clearance above the head for helmets, in world units, clamped on screen to [kMinGapPx, kMaxGapPx].
    static constexpr float kHelmetClearance = 16.f;
    static constexpr float kMinGapPx = 14.f;
    static constexpr float kMaxGapPx = 56.f;
    // Text scale = clamp(kScaleDistance / distance, kMinScale, kMaxScale).
    static constexpr float kScaleDistance = 800.f;
    static constexpr float kMinScale = 0.65f;
    static constexpr float kMaxScale = 1.2f;
    // UI font sizes are tuned for the overlay; tags match the HUD's activate prompt (16 -> 22 px).
    static constexpr float kFontSizeScale = 1.375f;
    static constexpr float kItalicShear = 0.2f;
    // Floating chat: shown for kLineBaseMs + kLinePerCharMs per character, at most kLineMaxMs,
    // wrapped at kLineWrapPx (at the 22 px base size) to kLineMaxLines lines.
    static constexpr uint64_t kLineBaseMs = 2000;
    static constexpr uint64_t kLinePerCharMs = 60;
    static constexpr uint64_t kLineMaxMs = 8000;
    static constexpr float kLineWrapPx = 300.f;
    static constexpr int kLineMaxLines = 2;
    static constexpr float kVisibilityEpsilon = 1e-3f;

private:
    struct VisibilityInfo
    {
        bool Visible = true;
        uint64_t Tick = 0;
    };

    struct FloatingLine
    {
        // Empty when the name is hidden; the stranger label is drawn instead.
        std::string Name;
        std::string Text;
        ChatMessageType Type = kGlobalChat;
        uint64_t ExpiresAt = 0;

        // Wrapped layout, rebuilt when LayoutSize or LayoutPrefix ("Name:") changes.
        struct LayoutLine
        {
            std::string Text;
            // Leading bytes of Text that belong to "Name:", drawn in the name colour.
            size_t NameBytes = 0;
            float NameWidth = 0.f;
            float Width = 0.f;
        };
        float LayoutSize = 0.f;
        std::string LayoutPrefix;
        LayoutLine Lines[kLineMaxLines];
        int LineCount = 0;
        float LineHeight = 0.f;
        float Width = 0.f;
    };

    void OnDraw() noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnChatMessage(const NotifyChatMessageBroadcast& acMessage) noexcept;

    void DrawFloatingLine(ImDrawList* apDrawList, ImFont* apFont, FloatingLine& aLine, const char* acpStrangerLabel, ImVec2 aBottomCenter, float aFontSize, float aAlpha, bool aBackdrop) const noexcept;
    static void LayoutFloatingLine(FloatingLine& aLine, ImFont* apFont, const std::string& acPrefix, float aFontSize) noexcept;

    [[nodiscard]] bool ShouldRenderTag(uint32_t aPlayerId) const noexcept;
    [[nodiscard]] bool ProjectWorldPoint(const NiPoint3& aWorldPoint, ImVec2& aScreenPos, float& aDepth, const ImVec2& aViewport) const noexcept;
    [[nodiscard]] NiPoint3 BuildHeadTopPoint(Actor* apActor) const noexcept;
    [[nodiscard]] static bool IsEnabledFor(Filter aFilter, bool aIsPartyMember) noexcept;
    [[nodiscard]] static ImU32 ColorWithAlpha(const ImVec4& aColor, float aAlpha) noexcept;

    World& m_world;

    Settings m_settings;
    std::unordered_map<uint32_t, VisibilityInfo> m_visibility;
    // Latest floating chat line per player id. Like m_visibility, it's written from dispatcher events and
    // read in OnDraw without a lock, which assumes the ImGui draw runs on the main thread (not verified).
    std::unordered_map<uint32_t, FloatingLine> m_floatingLines;

    entt::scoped_connection m_drawConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_chatConnection;
};
