#include <Services/LipSyncTogetherWindow.h>

#include <LipSyncTogether/LipSyncNatives.h>
#include <Services/ImguiService.h>

#include <imgui.h>
#include <imgui/imgui_impl_dx11.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
// --- look --------------------------------------------------------------------------------------------------------

// Modern pink, by Mailamea: a light blush window, white cards, a pink-to-magenta accent and plum text.
constexpr ImVec4 kBackground{1.f, 0.957f, 0.973f, 0.98f};    // #FFF4F8
constexpr ImVec4 kCard{1.f, 1.f, 1.f, 1.f};
constexpr ImVec4 kFrame{0.984f, 0.918f, 0.945f, 1.f};        // #FBEAF1
constexpr ImVec4 kFrameHovered{0.969f, 0.871f, 0.910f, 1.f};
constexpr ImVec4 kFrameActive{0.949f, 0.812f, 0.871f, 1.f};
constexpr ImVec4 kText{0.169f, 0.106f, 0.141f, 1.f};         // plum
constexpr ImVec4 kTextDim{0.545f, 0.427f, 0.490f, 1.f};      // muted rose
constexpr ImVec4 kAccent{0.910f, 0.267f, 0.494f, 1.f};       // #E8447E
constexpr ImVec4 kAccentBright{0.941f, 0.384f, 0.573f, 1.f}; // #F06292
constexpr ImVec4 kAccentDeep{0.761f, 0.094f, 0.357f, 1.f};   // #C2185B
constexpr ImVec4 kAccentSoft{0.910f, 0.267f, 0.494f, 0.14f};
constexpr ImVec4 kBorder{0.910f, 0.267f, 0.494f, 0.16f};
constexpr ImVec4 kShadow{0.761f, 0.094f, 0.357f, 0.07f};
constexpr ImVec4 kWhite{1.f, 1.f, 1.f, 1.f};
constexpr ImVec4 kGreen{0.18f, 0.66f, 0.42f, 1.f};
constexpr ImVec4 kAmber{0.86f, 0.52f, 0.10f, 1.f};
constexpr ImVec4 kRed{0.85f, 0.26f, 0.35f, 1.f};
constexpr ImVec4 kPurple{0.58f, 0.27f, 0.76f, 1.f};
constexpr ImVec4 kGrey{0.66f, 0.56f, 0.62f, 1.f};
constexpr ImVec4 kClear{0.f, 0.f, 0.f, 0.f};

constexpr const char* kPages[] = {"General", "Voice chat", "Hotkeys", "Advanced"};
constexpr const char* kWindowsDefault = "Windows default";
constexpr float kGameUnitsPerMetre = 70.f;

// Fonts, loaded between two frames the first time the window opens (Windows' own Segoe UI); null = ImGui's default.
std::atomic<bool> s_fontsWanted{false};
bool s_fontsTried = false;
ImFont* s_body = nullptr;
ImFont* s_small = nullptr;
ImFont* s_heading = nullptr;

struct FontScope
{
    explicit FontScope(ImFont* apFont) noexcept
        : m_pushed(apFont != nullptr)
    {
        if (m_pushed)
            ImGui::PushFont(apFont);
    }
    ~FontScope()
    {
        if (m_pushed)
            ImGui::PopFont();
    }
    bool m_pushed;
};

// The window's own look, pushed around it so Skyrim Together's other ImGui windows keep theirs.
struct Theme
{
    Theme() noexcept
    {
        const std::pair<ImGuiStyleVar, float> cFloats[] = {
            {ImGuiStyleVar_WindowRounding, 16.f}, {ImGuiStyleVar_FrameRounding, 7.f},     {ImGuiStyleVar_GrabRounding, 7.f},
            {ImGuiStyleVar_PopupRounding, 8.f},   {ImGuiStyleVar_ScrollbarRounding, 9.f}, {ImGuiStyleVar_ScrollbarSize, 9.f},
            {ImGuiStyleVar_WindowBorderSize, 1.f}, {ImGuiStyleVar_FrameBorderSize, 0.f},  {ImGuiStyleVar_GrabMinSize, 8.f},
        };
        for (const auto& [var, value] : cFloats)
            ImGui::PushStyleVar(var, value);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.f, 16.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 6.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 8.f));
        m_vars = static_cast<int>(std::size(cFloats)) + 3;

        const std::pair<ImGuiCol, ImVec4> cColours[] = {
            {ImGuiCol_WindowBg, kBackground},
            {ImGuiCol_ChildBg, kClear},
            {ImGuiCol_PopupBg, kCard},
            {ImGuiCol_Border, kBorder},
            {ImGuiCol_Text, kText},
            {ImGuiCol_TextDisabled, kTextDim},
            {ImGuiCol_FrameBg, kFrame},
            {ImGuiCol_FrameBgHovered, kFrameHovered},
            {ImGuiCol_FrameBgActive, kFrameActive},
            {ImGuiCol_Button, kFrame},
            {ImGuiCol_ButtonHovered, kFrameHovered},
            {ImGuiCol_ButtonActive, kFrameActive},
            {ImGuiCol_Header, kAccentSoft},
            {ImGuiCol_HeaderHovered, ImVec4(0.910f, 0.267f, 0.494f, 0.22f)},
            {ImGuiCol_HeaderActive, ImVec4(0.910f, 0.267f, 0.494f, 0.32f)},
            {ImGuiCol_SliderGrab, kAccent},
            {ImGuiCol_SliderGrabActive, kAccentDeep},
            {ImGuiCol_CheckMark, kAccent},
            {ImGuiCol_ScrollbarBg, kClear},
            {ImGuiCol_ScrollbarGrab, kFrameActive},
            {ImGuiCol_ScrollbarGrabHovered, kFrameHovered},
            {ImGuiCol_ScrollbarGrabActive, kFrameActive},
            {ImGuiCol_Separator, kBorder},
            {ImGuiCol_ResizeGrip, kAccentSoft},
            {ImGuiCol_ResizeGripHovered, kAccentSoft},
            {ImGuiCol_ResizeGripActive, kAccent},
        };
        for (const auto& [col, value] : cColours)
            ImGui::PushStyleColor(col, value);
        m_colours = static_cast<int>(std::size(cColours));
    }
    ~Theme()
    {
        ImGui::PopStyleColor(m_colours);
        ImGui::PopStyleVar(m_vars);
    }
    int m_vars{};
    int m_colours{};
};

ImU32 Colour(const ImVec4& acColour) noexcept
{
    return ImGui::GetColorU32(acColour);
}

// A card: a rounded panel behind a group of rows, drawn after its contents so it knows their height.
struct Card
{
    explicit Card(const char* acpTitle) noexcept
    {
        ImDrawList* pList = ImGui::GetWindowDrawList();
        pList->ChannelsSplit(2);
        pList->ChannelsSetCurrent(1);
        m_left = ImGui::GetCursorScreenPos().x;
        m_right = m_left + ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0.f, 2.f));
        ImGui::Indent(14.f);
        FontScope font(s_small);
        ImGui::TextColored(kAccentDeep, "%s", acpTitle);
    }
    ~Card()
    {
        ImGui::Unindent(14.f);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        ImGui::EndGroup();
        const ImVec2 cMin(m_left, ImGui::GetItemRectMin().y - 6.f);
        const ImVec2 cMax(m_right, ImGui::GetItemRectMax().y + 4.f);
        ImDrawList* pList = ImGui::GetWindowDrawList();
        pList->ChannelsSetCurrent(0);
        pList->AddRectFilled(ImVec2(cMin.x, cMin.y + 2.f), ImVec2(cMax.x, cMax.y + 3.f), Colour(kShadow), 12.f);
        pList->AddRectFilled(cMin, cMax, Colour(kCard), 12.f);
        pList->AddRect(cMin, cMax, Colour(kBorder), 12.f);
        pList->ChannelsMerge();
        ImGui::Dummy(ImVec2(0.f, 10.f));
    }
    float m_left{};
    float m_right{};
};

// Columns inside a card: label on the left, control on the right.
struct Row
{
    float LabelWidth;
    float ControlX;
    float ControlWidth;
};

Row MakeRow() noexcept
{
    const float cStart = ImGui::GetCursorPosX();
    const float cWidth = ImGui::GetContentRegionAvail().x - 14.f;
    const float cLabel = std::floor(cWidth * 0.48f);
    return Row{cLabel, cStart + cLabel + 8.f, cWidth - cLabel - 8.f};
}

void Help(const char* acpHelp) noexcept
{
    if (!acpHelp || !*acpHelp)
        return;
    FontScope font(s_small);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 14.f);
    ImGui::TextUnformatted(acpHelp);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// An on/off switch, right-aligned at aRightEdge.
bool Toggle(const char* acpId, bool& aValue, float aRightEdge) noexcept
{
    const float cHeight = ImGui::GetFrameHeight() * 0.82f;
    const float cWidth = cHeight * 1.9f;
    ImGui::SetCursorPosX(aRightEdge - cWidth);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - cHeight) * 0.5f);
    const ImVec2 cPos = ImGui::GetCursorScreenPos();
    const bool cClicked = ImGui::InvisibleButton(acpId, ImVec2(cWidth, cHeight));
    if (cClicked)
        aValue = !aValue;
    const bool cHovered = ImGui::IsItemHovered();
    ImDrawList* pList = ImGui::GetWindowDrawList();
    const ImVec4 cTrack = aValue ? (cHovered ? kAccentBright : kAccent) : (cHovered ? kFrameActive : ImVec4(0.925f, 0.835f, 0.875f, 1.f));
    pList->AddRectFilled(cPos, ImVec2(cPos.x + cWidth, cPos.y + cHeight), Colour(cTrack), cHeight * 0.5f);
    const float cRadius = cHeight * 0.5f - 3.f;
    const float cKnobX = aValue ? cPos.x + cWidth - cHeight * 0.5f : cPos.x + cHeight * 0.5f;
    pList->AddCircleFilled(ImVec2(cKnobX, cPos.y + cHeight * 0.5f + 1.f), cRadius, Colour(kShadow));
    pList->AddCircleFilled(ImVec2(cKnobX, cPos.y + cHeight * 0.5f), cRadius, Colour(kWhite));
    return cClicked;
}

void GradientBanner(const ImVec2& acMin, const ImVec2& acMax, float aRounding) noexcept
{
    ImDrawList* pList = ImGui::GetWindowDrawList();
    const ImU32 cLeft = Colour(kAccentBright);
    const ImU32 cRight = Colour(kAccentDeep);
    pList->AddRectFilled(ImVec2(acMin.x, acMin.y + 3.f), ImVec2(acMax.x, acMax.y + 4.f), Colour(kShadow), aRounding);
    pList->AddRectFilled(acMin, ImVec2(acMin.x + aRounding * 2.f, acMax.y), cLeft, aRounding, ImDrawFlags_RoundCornersLeft);
    pList->AddRectFilled(ImVec2(acMax.x - aRounding * 2.f, acMin.y), acMax, cRight, aRounding, ImDrawFlags_RoundCornersRight);
    pList->AddRectFilledMultiColor(ImVec2(acMin.x + aRounding, acMin.y), ImVec2(acMax.x - aRounding, acMax.y), cLeft, cRight, cRight, cLeft);
}

// A small rounded label with a tinted background.
void Pill(const char* acpText, const ImVec4& acColour) noexcept
{
    FontScope font(s_small);
    const ImVec2 cSize = ImGui::CalcTextSize(acpText);
    const ImVec2 cPos = ImGui::GetCursorScreenPos();
    const ImVec2 cPadding(9.f, 3.f);
    ImVec4 background = acColour;
    background.w = 0.20f;
    ImDrawList* pList = ImGui::GetWindowDrawList();
    pList->AddRectFilled(cPos, ImVec2(cPos.x + cSize.x + cPadding.x * 2, cPos.y + cSize.y + cPadding.y * 2), Colour(background), 99.f);
    pList->AddText(ImVec2(cPos.x + cPadding.x, cPos.y + cPadding.y), Colour(acColour), acpText);
    ImGui::Dummy(ImVec2(cSize.x + cPadding.x * 2, cSize.y + cPadding.y * 2));
}

void Dot(const ImVec4& acColour) noexcept
{
    const float cSize = ImGui::GetTextLineHeight();
    const ImVec2 cPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cPos.x + cSize * 0.4f, cPos.y + cSize * 0.55f), cSize * 0.22f, Colour(acColour));
    ImGui::Dummy(ImVec2(cSize * 0.8f, cSize));
    ImGui::SameLine();
}

std::string KeyName(uint32_t aKey)
{
    if (aKey == 0)
        return "Not set";
    char name[64]{};
    const UINT cScan = MapVirtualKeyA(aKey, MAPVK_VK_TO_VSC);
    if (cScan && GetKeyNameTextA(static_cast<LONG>(cScan << 16), name, sizeof(name)) > 0)
        return name;
    snprintf(name, sizeof(name), "Key 0x%02X", aKey);
    return name;
}

std::vector<std::string> Split(const char* acpText, char aSeparator)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char* p = acpText; p && *p; ++p)
    {
        if (*p == aSeparator)
        {
            if (!current.empty())
                parts.push_back(current);
            current.clear();
        }
        else if (*p != '\r')
            current.push_back(*p);
    }
    if (!current.empty())
        parts.push_back(current);
    return parts;
}

// Some drivers put "()" in front of their device names; drop it: "()Name (Driver)" -> "Name (Driver)".
std::string TidyDeviceName(const std::string& acName)
{
    std::string name = acName;
    if (name.rfind("()", 0) == 0)
        name.erase(0, 2);
    return name;
}

std::string ChoiceLabel(const std::string& acChoice)
{
    if (_stricmp(acChoice.c_str(), "Shapes") == 0)
        return "Jaw and lips";
    if (_stricmp(acChoice.c_str(), "Jaw") == 0)
        return "Jaw only";
    return acChoice;
}
} // namespace

void LipSyncTogether_BeforeImGuiFrame() noexcept
{
    if (!s_fontsWanted.load() || s_fontsTried)
        return;
    s_fontsTried = true;

    char windows[MAX_PATH]{};
    if (!GetWindowsDirectoryA(windows, MAX_PATH))
        return;
    const std::string cRegular = std::string(windows) + "\\Fonts\\segoeui.ttf";
    std::string bold = std::string(windows) + "\\Fonts\\seguisb.ttf";
    if (GetFileAttributesA(cRegular.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        spdlog::info("LipSync Together: Segoe UI not found; the settings window keeps ImGui's font");
        return;
    }
    if (GetFileAttributesA(bold.c_str()) == INVALID_FILE_ATTRIBUTES)
        bold = cRegular;

    ImGuiIO& io = ImGui::GetIO();
    // Keep the existing default font first, so Skyrim Together's own ImGui windows look the same.
    if (io.Fonts->Fonts.Size == 0)
        io.Fonts->AddFontDefault();
    s_body = io.Fonts->AddFontFromFileTTF(cRegular.c_str(), 17.f);
    s_small = io.Fonts->AddFontFromFileTTF(cRegular.c_str(), 14.5f);
    s_heading = io.Fonts->AddFontFromFileTTF(bold.c_str(), 21.f);
    // The font texture is rebuilt with the new fonts when this frame starts.
    ImGui_ImplDX11_InvalidateDeviceObjects();
    spdlog::info("LipSync Together: settings window fonts loaded");
}

LipSyncTogetherWindow::LipSyncTogetherWindow(ImguiService& aImgui) noexcept
{
    m_drawConnection = aImgui.OnDraw.connect<&LipSyncTogetherWindow::Draw>(this);
}

void LipSyncTogetherWindow::ReadDevices(const STRLipSync::Descriptor& acDescriptor) noexcept
{
    static char s_buffer[8192];
    m_inputDevices.clear();
    m_outputDevices.clear();
    if (acDescriptor.ListDevices(0, s_buffer, sizeof(s_buffer)))
        m_inputDevices = Split(s_buffer, '\n');
    if (acDescriptor.ListDevices(1, s_buffer, sizeof(s_buffer)))
        m_outputDevices = Split(s_buffer, '\n');
}

void LipSyncTogetherWindow::Draw() noexcept
{
    const STRLipSync::Descriptor* pDescriptor = m_pDescriptor.load();
    if (!Visible || !pDescriptor)
    {
        m_captureIndex = -1;
        CapturingKey = false;
        return;
    }
    const STRLipSync::Descriptor& descriptor = *pDescriptor;
    s_fontsWanted = true;

    if (RefreshDevices.exchange(false))
        ReadDevices(descriptor);

    // Sized to the screen (a Nucleus Co-op half screen included) and opened on the right, where Skyrim Together's own
    // menu is not; never saved, so it cannot reopen off screen.
    const ImGuiIO& io = ImGui::GetIO();
    const float cWidth = std::clamp(io.DisplaySize.x * 0.30f, std::min(440.f, io.DisplaySize.x - 32.f), 560.f);
    const float cHeight = std::min(720.f, io.DisplaySize.y - 32.f);
    ImGui::SetNextWindowSize(ImVec2(cWidth, cHeight), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - cWidth - 16.f, (io.DisplaySize.y - cHeight) * 0.5f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(380.f, 260.f), ImVec2(900.f, std::max(260.f, io.DisplaySize.y - 16.f)));

    Theme theme;
    FontScope bodyFont(s_body);
    bool close = false;
    if (ImGui::Begin("##LipSyncTogetherSettings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        // Header: a pink banner with the title, the author and version, and the close button.
        {
            const ImVec2 cMin = ImGui::GetCursorScreenPos();
            const ImVec2 cMax(cMin.x + ImGui::GetContentRegionAvail().x, cMin.y + 60.f);
            GradientBanner(cMin, cMax, 12.f);
            ImGui::SetCursorScreenPos(ImVec2(cMin.x + 16.f, cMin.y + 8.f));
            {
                FontScope font(s_heading);
                ImGui::TextColored(kWhite, "LipSync Together");
            }
            ImGui::SetCursorScreenPos(ImVec2(cMin.x + 16.f, ImGui::GetCursorScreenPos().y - 2.f));
            {
                FontScope font(s_small);
                ImGui::TextColored(ImVec4(1.f, 1.f, 1.f, 0.85f), "by Mailamea   |   %s", descriptor.ModVersion ? descriptor.ModVersion : "");
            }
            const float cClose = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(cMax.x - cClose - 12.f, cMin.y + (cMax.y - cMin.y - cClose) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, kClear);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, 1.f, 1.f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.f, 1.f, 1.f, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, cClose * 0.5f);
            if (ImGui::Button("X##close", ImVec2(cClose, cClose)))
                close = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            ImGui::SetCursorScreenPos(ImVec2(cMin.x, cMax.y + 12.f));
        }

        DrawMeter(descriptor);

        // Pages.
        ImGui::Spacing();
        for (int i = 0; i < static_cast<int>(std::size(kPages)); ++i)
        {
            if (i > 0)
                ImGui::SameLine();
            const bool cSelected = m_page == i;
            ImGui::PushStyleColor(ImGuiCol_Button, cSelected ? kAccent : kClear);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cSelected ? kAccentBright : kAccentSoft);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, cSelected ? kAccentDeep : kFrameActive);
            ImGui::PushStyleColor(ImGuiCol_Text, cSelected ? kWhite : kTextDim);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 99.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.f, 6.f));
            if (ImGui::Button(kPages[i]))
                m_page = i;
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }
        ImGui::Separator();

        // The page's cards, scrolling between the header and the footer.
        const float cFooter = ImGui::GetFrameHeightWithSpacing() + 18.f;
        ImGui::BeginChild("##Page", ImVec2(0.f, -cFooter), false);
        ImGui::Dummy(ImVec2(0.f, 6.f));
        if (strcmp(kPages[m_page], "Voice chat") == 0)
            DrawVoiceStatus(descriptor);

        const uint32_t cCount = descriptor.GetSettingCount();
        std::string group;
        std::unique_ptr<Card> card;
        for (uint32_t i = 0; i < cCount; ++i)
        {
            const STRLipSync::SettingInfo* pInfo = descriptor.GetSettingInfo(i);
            if (!pInfo || !pInfo->Page || strcmp(pInfo->Page, kPages[m_page]) != 0)
                continue;
            const std::string cGroup = pInfo->Group ? pInfo->Group : "";
            if (!card || group != cGroup)
            {
                card.reset();
                group = cGroup;
                card = std::make_unique<Card>(group.c_str());
            }
            DrawSetting(descriptor, i);
        }
        card.reset();
        ImGui::EndChild();

        // Footer: saved state, Revert, Save, Close.
        ImGui::Separator();
        const bool cUnsaved = descriptor.HasUnsavedChanges() != 0;
        {
            FontScope font(s_small);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f);
            if (cUnsaved)
                ImGui::TextColored(kAmber, "Changes are live, not saved yet");
            else
                ImGui::TextColored(kTextDim, "All changes saved");
        }
        const float cButton = 78.f;
        const float cButtons = 3 * cButton + 2 * ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - cButtons);
        ImGui::BeginDisabled(!cUnsaved);
        if (ImGui::Button("Revert", ImVec2(cButton, 0.f)))
        {
            descriptor.ReloadSettings();
            m_dragging.clear();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, cUnsaved ? kAccent : kFrame);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cUnsaved ? kAccentBright : kFrameHovered);
        ImGui::PushStyleColor(ImGuiCol_Text, cUnsaved ? kWhite : kTextDim);
        if (ImGui::Button("Save", ImVec2(cButton, 0.f)))
            descriptor.SaveSettings();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(cButton, 0.f)))
            close = true;
    }
    ImGui::End();

    if (close)
        CloseRequested = true;
}

void LipSyncTogetherWindow::DrawMeter(const STRLipSync::Descriptor& acDescriptor) noexcept
{
    STRLipSync::Meter meter{};
    const bool cHave = acDescriptor.GetMeter(&meter) != 0;

    // Status: a pill and a short line.
    if (!cHave)
        Pill("NO MICROPHONE", kRed);
    else if (meter.Whispering)
        Pill("WHISPERING", kPurple);
    else if (meter.Speaking)
        Pill("SPEAKING", kAccent);
    else
        Pill("QUIET", kGrey);
    ImGui::SameLine();
    {
        FontScope font(s_small);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
        if (!cHave)
            ImGui::TextColored(kTextDim, "Pick a microphone on the General page.");
        else if (meter.Calibration < 1.f)
            ImGui::TextColored(kTextDim, "Learning your normal voice... %.0f%%", meter.Calibration * 100.f);
        else if (meter.Range > 1.3f)
            ImGui::TextColored(kTextDim, "Yelling: heard %.1fx further", meter.Range);
        else if (meter.Range < 0.7f)
            ImGui::TextColored(kTextDim, "Soft voice: heard only nearby");
        else
            ImGui::TextColored(kTextDim, "Normal range");
    }

    // Level bar with the gate marked: green when the voice counts.
    const float cWidth = ImGui::GetContentRegionAvail().x;
    const float cHeight = 8.f;
    ImGui::Dummy(ImVec2(0.f, 2.f));
    const ImVec2 cPos = ImGui::GetCursorScreenPos();
    auto toX = [&](float aDb) { return cPos.x + cWidth * std::clamp((aDb + 80.f) / 80.f, 0.f, 1.f); };
    ImDrawList* pList = ImGui::GetWindowDrawList();
    pList->AddRectFilled(cPos, ImVec2(cPos.x + cWidth, cPos.y + cHeight), Colour(kFrame), cHeight * 0.5f);
    if (cHave)
    {
        const bool cOver = meter.LevelDb > meter.GateDb;
        const float cEnd = std::max(cPos.x + cHeight, toX(meter.LevelDb));
        if (cOver)
        {
            pList->AddRectFilled(cPos, ImVec2(cPos.x + cHeight, cPos.y + cHeight), Colour(kAccentBright), cHeight * 0.5f, ImDrawFlags_RoundCornersLeft);
            pList->AddRectFilledMultiColor(ImVec2(cPos.x + cHeight * 0.5f, cPos.y), ImVec2(cEnd - cHeight * 0.5f, cPos.y + cHeight), Colour(kAccentBright),
                                           Colour(kAccent), Colour(kAccent), Colour(kAccentBright));
            pList->AddRectFilled(ImVec2(cEnd - cHeight, cPos.y), ImVec2(cEnd, cPos.y + cHeight), Colour(kAccent), cHeight * 0.5f, ImDrawFlags_RoundCornersRight);
        }
        else
            pList->AddRectFilled(cPos, ImVec2(cEnd, cPos.y + cHeight), Colour(kGrey), cHeight * 0.5f);
        const float cGate = toX(meter.GateDb);
        pList->AddRectFilled(ImVec2(cGate - 1.f, cPos.y - 3.f), ImVec2(cGate + 1.f, cPos.y + cHeight + 3.f), Colour(kAccentDeep), 1.f);
    }
    ImGui::Dummy(ImVec2(cWidth, cHeight + 2.f));
    if (cHave && ImGui::GetIO().DisplaySize.y >= 700.f)
    {
        FontScope font(s_small);
        ImGui::TextColored(kTextDim, "Level %.0f dB   |   needed %.0f dB   |   voice %.0f%%", meter.LevelDb, meter.GateDb, meter.Voicing * 100.f);
    }
}

void LipSyncTogetherWindow::DrawVoiceStatus(const STRLipSync::Descriptor& acDescriptor) noexcept
{
    static char s_status[2048];
    if (!acDescriptor.GetVoiceStatus(s_status, sizeof(s_status)))
        return;
    Card card("Status");
    FontScope font(s_small);
    for (const auto& line : Split(s_status, '\n'))
    {
        ImVec4 colour = kGrey;
        if (line.find("Hosting") != std::string::npos || line.find("Connected") != std::string::npos || line.find(": talking") != std::string::npos)
            colour = kGreen;
        else if (line.find("Waiting") != std::string::npos || line.find("muted") != std::string::npos)
            colour = kAmber;
        Dot(colour);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 14.f);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopTextWrapPos();
    }
}

void LipSyncTogetherWindow::DrawSetting(const STRLipSync::Descriptor& acDescriptor, uint32_t aIndex) noexcept
{
    const STRLipSync::SettingInfo* pInfo = acDescriptor.GetSettingInfo(aIndex);
    if (!pInfo)
        return;
    char value[512]{};
    acDescriptor.GetSetting(aIndex, value, sizeof(value));
    const char* pLabel = pInfo->Label ? pInfo->Label : pInfo->Key;
    const char* pUnit = pInfo->Unit ? pInfo->Unit : "";

    ImGui::PushID(static_cast<int>(aIndex));
    const Row cRow = MakeRow();

    ImGui::AlignTextToFramePadding();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cRow.LabelWidth);
    ImGui::TextUnformatted(pLabel);
    ImGui::PopTextWrapPos();
    ImGui::SameLine(cRow.ControlX);

    switch (pInfo->Type)
    {
    case STRLipSync::kSettingBool:
    {
        bool on = value[0] == '1';
        if (Toggle("##toggle", on, cRow.ControlX + cRow.ControlWidth))
            acDescriptor.SetSetting(aIndex, on ? "1" : "0");
        break;
    }
    case STRLipSync::kSettingInt:
    case STRLipSync::kSettingFloat:
    {
        // Shown in friendlier units (metres, kbit/s); applied when the slider is let go, so a drag does not restart
        // the relay or fill the log at every step.
        const bool cMetres = strcmp(pUnit, "units") == 0;
        const bool cKbit = strcmp(pUnit, "bit/s") == 0;
        const float cScale = cMetres ? 1.f / kGameUnitsPerMetre : (cKbit ? 0.001f : 1.f);
        auto it = m_dragging.find(aIndex);
        float shown = it != m_dragging.end() ? it->second : static_cast<float>(atof(value)) * cScale;
        const float cMin = pInfo->Min * cScale;
        const float cMax = pInfo->Max * cScale;

        const char* pFormat = pInfo->Type == STRLipSync::kSettingInt ? "%.0f" : "%.2f";
        if (cMetres)
            pFormat = "%.0f m";
        else if (cKbit)
            pFormat = "%.0f kbit/s";
        else if (strcmp(pUnit, "dB") == 0)
            pFormat = "%.0f dB";
        else if (strcmp(pUnit, "ms") == 0)
            pFormat = "%.0f ms";
        else if (strcmp(pUnit, "x") == 0)
            pFormat = "%.2fx";

        // Distances span far more than anyone uses, so they get a fine drag rather than a coarse slider; drags show
        // their value inside, sliders beside them so the handle never covers it.
        const bool cDrag = cMetres || cMax - cMin > 2000.f;
        if (cDrag)
        {
            ImGui::SetNextItemWidth(cRow.ControlWidth);
            ImGui::DragFloat("##value", &shown, cMetres ? 0.25f : (cMax - cMin) / 3000.f, cMin, cMax, pFormat, ImGuiSliderFlags_AlwaysClamp);
        }
        else
        {
            const float cValueWidth = 76.f;
            ImGui::SetNextItemWidth(cRow.ControlWidth - cValueWidth - 8.f);
            ImGui::SliderFloat("##value", &shown, cMin, cMax, "", ImGuiSliderFlags_AlwaysClamp);
        }
        const bool cActive = ImGui::IsItemActive();
        const bool cReleased = ImGui::IsItemDeactivatedAfterEdit();
        if (!cDrag)
        {
            char text[32];
            snprintf(text, sizeof(text), pFormat, shown);
            ImGui::SameLine(cRow.ControlX + cRow.ControlWidth - ImGui::CalcTextSize(text).x);
            ImGui::TextColored(cActive ? kAccentDeep : kText, "%s", text);
        }

        if (cActive)
            m_dragging[aIndex] = shown;
        if (cReleased)
        {
            char text[32];
            const float cStored = shown / cScale;
            if (pInfo->Type == STRLipSync::kSettingInt)
                snprintf(text, sizeof(text), "%d", static_cast<int>(std::lround(cStored)));
            else
                snprintf(text, sizeof(text), "%g", cStored);
            acDescriptor.SetSetting(aIndex, text);
            m_dragging.erase(aIndex);
        }
        else if (!cActive)
            m_dragging.erase(aIndex);
        break;
    }
    case STRLipSync::kSettingKey:
        DrawKey(acDescriptor, aIndex, cRow.ControlWidth, value);
        break;
    case STRLipSync::kSettingInputDevice:
    case STRLipSync::kSettingOutputDevice:
        DrawDevice(acDescriptor, aIndex, *pInfo, cRow.ControlWidth, value);
        break;
    case STRLipSync::kSettingChoice:
    {
        ImGui::SetNextItemWidth(cRow.ControlWidth);
        if (ImGui::BeginCombo("##choice", ChoiceLabel(value).c_str()))
        {
            for (const auto& choice : Split(pInfo->Choices, '|'))
            {
                if (ImGui::Selectable(ChoiceLabel(choice).c_str(), _stricmp(choice.c_str(), value) == 0))
                    acDescriptor.SetSetting(aIndex, choice.c_str());
            }
            ImGui::EndCombo();
        }
        break;
    }
    default: ImGui::TextUnformatted(value); break;
    }

    Help(pInfo->Help);
    ImGui::PopID();
}

void LipSyncTogetherWindow::DrawKey(const STRLipSync::Descriptor& acDescriptor, uint32_t aIndex, float aWidth, const char* acpValue) noexcept
{
    const uint32_t cKey = static_cast<uint32_t>(strtoul(acpValue, nullptr, 0));
    if (m_captureIndex == static_cast<int>(aIndex))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccentSoft);
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentDeep);
        ImGui::Button("Press a key...##key", ImVec2(aWidth, 0.f));
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Esc: cancel   Backspace: no key");

        // The next key that goes down (not one already held when the button was clicked) is the one.
        for (uint32_t vk = 0x08; vk < 0xFF; ++vk)
        {
            const bool cDown = (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
            if (!cDown)
            {
                m_heldAtCapture[vk] = false;
                continue;
            }
            if (m_heldAtCapture[vk])
                continue;
            if (vk == VK_ESCAPE)
                m_captureIndex = -1;
            else if (vk == VK_BACK || vk == VK_DELETE)
            {
                acDescriptor.SetSetting(aIndex, "0");
                m_captureIndex = -1;
            }
            else if (vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU && vk != VK_LSHIFT && vk != VK_RSHIFT && vk != VK_LCONTROL && vk != VK_RCONTROL &&
                     vk != VK_LMENU && vk != VK_RMENU)
            {
                char text[16];
                snprintf(text, sizeof(text), "0x%02X", vk);
                acDescriptor.SetSetting(aIndex, text);
                m_captureIndex = -1;
            }
            break;
        }
        CapturingKey = m_captureIndex >= 0;
        return;
    }

    const std::string cButton = KeyName(cKey) + "##key";
    ImGui::PushStyleColor(ImGuiCol_Text, cKey ? kText : kTextDim);
    const bool cClicked = ImGui::Button(cButton.c_str(), ImVec2(aWidth, 0.f));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Click, then press the key");
    if (cClicked && m_captureIndex < 0)
    {
        m_captureIndex = static_cast<int>(aIndex);
        for (uint32_t vk = 0; vk < 256; ++vk)
            m_heldAtCapture[vk] = (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
        CapturingKey = true;
    }
}

void LipSyncTogetherWindow::DrawDevice(const STRLipSync::Descriptor& acDescriptor, uint32_t aIndex, const STRLipSync::SettingInfo& acInfo, float aWidth,
                                       const char* acpValue) noexcept
{
    const std::string cPreview = *acpValue ? TidyDeviceName(acpValue) : kWindowsDefault;
    ImGui::SetNextItemWidth(aWidth);
    if (ImGui::BeginCombo("##device", cPreview.c_str()))
    {
        // Read the devices again whenever the list opens, so a headset plugged in meanwhile shows up.
        if (ImGui::IsWindowAppearing())
            ReadDevices(acDescriptor);
        const auto& devices = acInfo.Type == STRLipSync::kSettingOutputDevice ? m_outputDevices : m_inputDevices;
        if (ImGui::Selectable(kWindowsDefault, *acpValue == 0))
            acDescriptor.SetSetting(aIndex, "");
        for (const auto& device : devices)
        {
            // The setting is part of a name; it matches a device that contains it.
            const bool cSelected = *acpValue && device.find(acpValue) != std::string::npos;
            if (ImGui::Selectable(TidyDeviceName(device).c_str(), cSelected))
                acDescriptor.SetSetting(aIndex, device.c_str());
        }
        ImGui::EndCombo();
    }
}
