#include "ImguiUI.h"
#include <cmath>
#include "UIWidgets.h"
#include "ResolutionEditor.h"

#include <spdlog/spdlog.h>
#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "libultraship/src/Context.h"

#include <imgui_internal.h>
#include <libultraship/libultraship.h>
#include <Fast3D/interpreter.h>
#include "port/Engine.h"
#include "port/notification/notification.h"
#include "utils/StringHelper.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#include <port/switch/SwitchPerformanceProfiles.h>
#endif

extern "C" {
#include "sys.h"
#include <sf64audio_provisional.h>
#include <sf64context.h>
}

// Switch 2 menus: the font is 1.5x and the padding 3x, so widen the fixed menu widths.
#ifdef __SWITCH2__
static constexpr float kSwitch2StyleScale = 3.0f;
static constexpr float kMenuWidthScale = 2.0f;
#else
static constexpr float kMenuWidthScale = 1.0f;
#endif

namespace GameUI {
std::shared_ptr<GameMenuBar> mGameMenuBar;
std::shared_ptr<Ship::GuiWindow> mConsoleWindow;
std::shared_ptr<Ship::GuiWindow> mStatsWindow;
std::shared_ptr<Ship::GuiWindow> mInputEditorWindow;
std::shared_ptr<Ship::GuiWindow> mGfxDebuggerWindow;
std::shared_ptr<Notification::Window> mNotificationWindow;
std::shared_ptr<AdvancedResolutionSettings::AdvancedResolutionSettingsWindow> mAdvancedResolutionSettingsWindow;

void SetupGuiElements() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();

    auto& style = ImGui::GetStyle();
#ifdef __SWITCH2__
    // Same scale as the base style: these values replace the scaled ones.
    style.FramePadding = ImVec2(4.0f * kSwitch2StyleScale, 6.0f * kSwitch2StyleScale);
    style.ItemSpacing = ImVec2(8.0f * kSwitch2StyleScale, 6.0f * kSwitch2StyleScale);
#else
    style.FramePadding = ImVec2(4.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
#endif
    style.Colors[ImGuiCol_MenuBarBg] = UIWidgets::Colors::DarkGray;

#ifdef __SWITCH2__
    // The full-screen menu always starts closed, even if it was open at the last quit.
    CVarClear("gOpenMenuBar");
#endif
    mGameMenuBar = std::make_shared<GameMenuBar>("gOpenMenuBar", CVarGetInteger("gOpenMenuBar", 0));
    gui->SetMenuBar(mGameMenuBar);

    if (gui->GetMenuBar() && !gui->GetMenuBar()->IsVisible()) {
#if defined(__SWITCH__) || defined(__WIIU__)
        Notification::Emit({ .message = "Press - to access enhancements menu", .remainingTime = 10.0f });
#else
        Notification::Emit({ .message = "Press F1 to access enhancements menu", .remainingTime = 10.0f });
#endif
    }

    mStatsWindow = gui->GetGuiWindow("Stats");
    if (mStatsWindow == nullptr) {
        SPDLOG_ERROR("Could not find stats window");
    }

    mConsoleWindow = gui->GetGuiWindow("Console");
    if (mConsoleWindow == nullptr) {
        SPDLOG_ERROR("Could not find console window");
    }

    mInputEditorWindow = gui->GetGuiWindow("Input Editor");
    if (mInputEditorWindow == nullptr) {
        SPDLOG_ERROR("Could not find input editor window");
        return;
    }

    mGfxDebuggerWindow = gui->GetGuiWindow("GfxDebuggerWindow");
    if (mGfxDebuggerWindow == nullptr) {
        SPDLOG_ERROR("Could not find input GfxDebuggerWindow");
    }

    mAdvancedResolutionSettingsWindow = std::make_shared<AdvancedResolutionSettings::AdvancedResolutionSettingsWindow>("gAdvancedResolutionEditorEnabled", "Advanced Resolution Settings");
    gui->AddGuiWindow(mAdvancedResolutionSettingsWindow);
    mNotificationWindow = std::make_shared<Notification::Window>("gNotifications", "Notifications Window");
    gui->AddGuiWindow(mNotificationWindow);
    mNotificationWindow->Show();
}

void Destroy() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    gui->RemoveAllGuiWindows();

    mAdvancedResolutionSettingsWindow = nullptr;
    mConsoleWindow = nullptr;
    mStatsWindow = nullptr;
    mInputEditorWindow = nullptr;
    mNotificationWindow = nullptr;
    mGfxDebuggerWindow = nullptr;
    gui->SetMenuBar(nullptr);
    mGameMenuBar = nullptr;
}

std::string GetWindowButtonText(const char* text, bool menuOpen) {
    char buttonText[100] = "";
    if (menuOpen) {
        strcat(buttonText, ICON_FA_CHEVRON_RIGHT " ");
    }
    strcat(buttonText, text);
    if (!menuOpen) { strcat(buttonText, "  "); }
    return buttonText;
}
}

static const char* filters[3] = {
#ifdef __WIIU__
        "",
#else
        "Three-Point",
#endif
        "Linear", "None"
};

// The game's volume scale is 0 to 99. Rounding keeps a value from dropping on the next boot.
static float VolumeSliderToGame(float value) {
#ifdef __SWITCH__
    return std::round(value * 99.0f);
#else
    return value * 100;
#endif
}

static const char* voiceLangs[] = {
    "Original", /*"Japanese",*/ "Lylat"
};
#ifdef __SWITCH2__
// The full-screen menu reuses the menu bar's contents: top-level menus become
// header tabs, and their submenus become sidebar sections.
namespace StarshipMenu {
static std::string sTab = "Settings";
static std::string sSection;
static std::map<std::string, std::vector<std::string>> sSections;
static std::map<std::string, std::vector<std::string>> sNextSections;
static std::vector<int> sDepthStack;
static int sDepth = 0;
static std::string sDrawTab;
static ImGuiTextFilter sSearch;
static std::string sSearchText;
static bool sFiltering = false;
static bool sLastShown = true;
static int sMatches = 0;

static std::string Normalized(const char* text) {
    std::string out;
    for (const char* c = text; *c != '\0' && !(c[0] == '#' && c[1] == '#'); c++) {
        if (*c != ' ') {
            out += static_cast<char>(tolower(static_cast<unsigned char>(*c)));
        }
    }
    return out;
}

static bool Matches(const char* label) {
    return Normalized(label).find(sSearchText) != std::string::npos;
}

static void AddSection(const std::string& tab, const char* label) {
    auto& list = sNextSections[tab];
    if (std::find(list.begin(), list.end(), label) == list.end()) {
        list.push_back(label);
    }
}

static bool Open(int depth) {
    sDepthStack.push_back(sDepth);
    sDepth = depth;
    return true;
}
} // namespace StarshipMenu

namespace UIWidgets {
static bool StarshipBeginMenu(const char* label, const ImVec4& = ImVec4()) {
    using namespace StarshipMenu;
    if (sDepth == 0) {
        const bool graphics = strcmp(label, "Graphics") == 0;
        sDrawTab = graphics ? "Settings" : label;
        if (graphics) {
            AddSection("Settings", label);
            return (sFiltering || (sTab == "Settings" && sSection == label)) && Open(2);
        }
        return (sFiltering || sTab == label) && Open(1);
    }
    AddSection(sDrawTab, label);
    return (sFiltering || sSection == label) && Open(2);
}

static void StarshipSpacer(float height) {
    // Spacers between submenus would only push the open section down.
    if (StarshipMenu::sFiltering || (StarshipMenu::sDepth <= 1 && StarshipMenu::sTab == "Settings")) {
        return;
    }
    Spacer(height);
}

// Whether a control with this label is shown by the current search.
static bool StarshipShow(const char* label) {
    using namespace StarshipMenu;
    if (!sFiltering) {
        return true;
    }
    sLastShown = Matches(label);
    sMatches += sLastShown ? 1 : 0;
    return sLastShown;
}

// For a control whose label is the Text line drawn just before it.
static bool StarshipShowLast() {
    return !StarshipMenu::sFiltering || StarshipMenu::sLastShown;
}

static void StarshipTooltip(const char* text) {
    if (StarshipShowLast()) {
        Tooltip(text);
    }
}

static void StarshipPaddedSeparator(bool padTop = true, bool padBottom = true, float extraTop = 0.0f,
                                    float extraBottom = 0.0f) {
    if (!StarshipMenu::sFiltering) {
        PaddedSeparator(padTop, padBottom, extraTop, extraBottom);
    }
}
} // namespace UIWidgets

namespace ImGui {
static void StarshipEndMenu() {
    StarshipMenu::sDepth = StarshipMenu::sDepthStack.back();
    StarshipMenu::sDepthStack.pop_back();
}

static void StarshipText(const char* format, ...) IM_FMTARGS(1);
static void StarshipText(const char* format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (UIWidgets::StarshipShow(text)) {
        TextUnformatted(text);
    }
}

static bool StarshipBeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0) {
    return UIWidgets::StarshipShowLast() && BeginCombo(label, preview, flags);
}

static void StarshipDummy(const ImVec2& size) {
    if (!StarshipMenu::sFiltering) {
        Dummy(size);
    }
}

static void StarshipSameLine(float offset = 0.0f, float spacing = -1.0f) {
    if (!StarshipMenu::sFiltering) {
        SameLine(offset, spacing);
    }
}
} // namespace ImGui

// The menu's controls are drawn straight from code, with no list of settings to
// search, so each control asks the search whether to draw itself.
#define BeginMenu StarshipBeginMenu
#define EndMenu StarshipEndMenu
#define Spacer StarshipSpacer
#define Tooltip StarshipTooltip
#define PaddedSeparator StarshipPaddedSeparator
#define Text StarshipText
#define BeginCombo StarshipBeginCombo
#define Dummy StarshipDummy
#define SameLine StarshipSameLine
#define CVarCheckbox(label, ...) StarshipShow(label) && UIWidgets::CVarCheckbox(label, __VA_ARGS__)
#define CVarSliderInt(label, ...) StarshipShow(label) && UIWidgets::CVarSliderInt(label, __VA_ARGS__)
#define CVarSliderFloat(label, ...) StarshipShow(label) && UIWidgets::CVarSliderFloat(label, __VA_ARGS__)
#define CVarCombobox(label, ...) StarshipShow(label) && UIWidgets::CVarCombobox(label, __VA_ARGS__)
#define PaddedEnhancementCheckbox(label, ...) \
    StarshipShow(label) && UIWidgets::PaddedEnhancementCheckbox(label, __VA_ARGS__)
#define WindowButton(label, ...) StarshipShow(label) && UIWidgets::WindowButton(label, __VA_ARGS__)
#define EnhancementCombobox(...) StarshipShowLast() && UIWidgets::EnhancementCombobox(__VA_ARGS__)
#endif

static const char* voiceLangsSPA[] = {
    "Español", /*"Japanese",*/ "Lylat"
};

void DrawSpeakerPositionEditor() {
#ifdef __SWITCH2__
    if (StarshipMenu::sFiltering) {
        return;
    }
#endif
    static ImVec2 lastCanvasPos;
    ImGui::Text("Speaker Position Editor");
    ImVec2 canvasSize = ImVec2(200, 200); // Static canvas size
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2);

    // Speaker positions
    static ImVec2 speakerPositions[4];
    static bool initialized = false;
    static float radius = 80.0f;

    // Reset positions if canvas position changed (window resized/moved)
    if (!initialized || (lastCanvasPos.x != canvasPos.x || lastCanvasPos.y != canvasPos.y)) {
        const char* cvarNames[4] = { "gPositionFrontLeft", "gPositionFrontRight", "gPositionRearLeft", "gPositionRearRight" };
        float angles[4] = { 240.f, 300.f, 160.f, 20.f }; // Default angles
        
        for (int i = 0; i < 4; i++) {
            int savedAngle = CVarGetInteger(cvarNames[i], -1);
            if (savedAngle != -1) {
                angles[i] = static_cast<float>(savedAngle);
            }

            float rad = angles[i] * (M_PI / 180.0f);
            speakerPositions[i] = ImVec2(center.x + radius * cosf(rad), center.y + radius * sinf(rad));
        }
        initialized = true;
        lastCanvasPos = canvasPos;
    }

    // Draw canvas
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(26, 26, 26, 255));
    drawList->AddCircleFilled(center, 5.0f, IM_COL32(255, 255, 255, 255)); // Central person

    // Draw circle line for speaker positions
    drawList->AddCircle(center, radius, IM_COL32(163, 163, 163, 255), 100);

    // Add markers at 0, 22.5, 45, etc.
    for (float angle = 0; angle < 360; angle += 22.5f) {
        float rad = angle * (M_PI / 180.0f);
        ImVec2 markerStart = ImVec2(center.x + (radius - 5) * cosf(rad), center.y + (radius - 5) * sinf(rad));
        ImVec2 markerEnd = ImVec2(center.x + radius * cosf(rad), center.y + radius * sinf(rad));
        drawList->AddLine(markerStart, markerEnd, IM_COL32(163, 163, 163, 255));
    }

    const char* speakerLabels[4] = { "L", "R", "RL", "RR" };
    const char* cvarNames[4] = { "gPositionFrontLeft", "gPositionFrontRight", "gPositionRearLeft", "gPositionRearRight" };

    const float snapThreshold = 2.5f; // Degrees within which snapping occurs

    for (int i = 0; i < 4; i++) {
        // Draw speaker as a darker blue circle
        drawList->AddCircleFilled(speakerPositions[i], 10.0f, IM_COL32(34, 52, 78, 255)); // Dark blue color
        drawList->AddText(ImVec2(speakerPositions[i].x - 6, speakerPositions[i].y - 6), IM_COL32(255, 255, 255, 255), speakerLabels[i]);

        // Handle dragging
        ImGui::SetCursorScreenPos(ImVec2(speakerPositions[i].x - 10, speakerPositions[i].y - 10));
        ImGui::InvisibleButton(speakerLabels[i], ImVec2(20, 20));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 mouseDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            ImVec2 newPos = ImVec2(speakerPositions[i].x + mouseDelta.x, speakerPositions[i].y + mouseDelta.y);

            // Constrain position to the circle
            ImVec2 direction = ImVec2(newPos.x - center.x, newPos.y - center.y);
            float length = sqrtf(direction.x * direction.x + direction.y * direction.y);
            ImVec2 constrainedPos = ImVec2(center.x + (direction.x / length) * radius, center.y + (direction.y / length) * radius);

            // Calculate angle of the constrained position
            float angle = atan2f(constrainedPos.y - center.y, constrainedPos.x - center.x) * (180.0f / M_PI);
            if (angle < 0) angle += 360.0f;

            // Snap to the nearest 22.5-degree marker if within the snap threshold
            float snappedAngle = roundf(angle / 22.5f) * 22.5f;
            if (fabsf(snappedAngle - angle) <= snapThreshold) {
                float rad = snappedAngle * (M_PI / 180.0f);
                constrainedPos = ImVec2(center.x + radius * cosf(rad), center.y + radius * sinf(rad));
            }

            speakerPositions[i] = constrainedPos;
            ImGui::ResetMouseDragDelta();

            // Save the updated angle to CVar after dragging
            float updatedAngle = atan2f(speakerPositions[i].y - center.y, speakerPositions[i].x - center.x) * (180.0f / M_PI);
            if (updatedAngle < 0) updatedAngle += 360.0f;
            CVarSetInteger(cvarNames[i], static_cast<int>(updatedAngle));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame(); // Mark for saving
        }

        // Calculate angle and save to CVar
        float angle = atan2f(speakerPositions[i].y - center.y, speakerPositions[i].x - center.x) * (180.0f / M_PI);
        if (angle < 0) angle += 360.0f;
        CVarSetInteger(cvarNames[i], static_cast<int>(angle));
    }

    // Reset cursor position for button placement
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y + 10));
    if (ImGui::Button("Reset Positions")) {
        float defaultAngles[4] = { 240.f, 300.f, 160.f, 20.f };
        for (int i = 0; i < 4; i++) {
            float rad = defaultAngles[i] * (M_PI / 180.0f);
            speakerPositions[i] = ImVec2(center.x + radius * cosf(rad), center.y + radius * sinf(rad));
            CVarSetInteger(cvarNames[i], static_cast<int>(defaultAngles[i]));
        }
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    // Reset cursor position to ensure canvas size remains static
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y + 10));
}

#ifdef __SWITCH__
#pragma push_macro("BeginCombo")
#undef BeginCombo
// The voice language, and the install of the JP and EU voices.
static void DrawVoiceInstallControls() {
    static const char* sInstallMessage = nullptr;
    static const char* const kVoiceNames[] = { "English", "Japanese", "European (Lylat Wars)" };

#ifdef __SWITCH2__
    if (UIWidgets::StarshipShow("Voices"))
#endif
    {
        int pack = CVarGetInteger("gVoicePack", 0);
        if (pack < 0 || pack > 2 || (pack != 0 && !GameEngine::IsVoiceInstalled(pack))) {
            pack = 0;
        }
        ImGui::TextUnformatted("Voices");
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 520.0f));
        if (ImGui::BeginCombo("##Voices", kVoiceNames[pack])) {
            for (int option = 0; option < 3; option++) {
                const bool available = option == 0 || GameEngine::IsVoiceInstalled(option);
                const std::string label =
                    std::string(kVoiceNames[option]) + (available ? "" : " (not installed)") + "##Voice";
                if (ImGui::Selectable(label.c_str(), pack == option,
                                      available ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled)) {
                    CVarSetInteger("gVoicePack", option);
                    CVarSave();
                    pack = option;
                }
            }
            ImGui::EndCombo();
        }
        if (pack != GameEngine::ActiveVoicePack()) {
            ImGui::TextDisabled("Restart Starship to use these voices.");
        }
        ImGui::Spacing();
    }

#ifdef __SWITCH2__
    if (UIWidgets::StarshipShow("Install JP/EU Audio"))
#endif
    {
        const bool allInstalled = GameEngine::IsVoiceInstalled(1) && GameEngine::IsVoiceInstalled(2);
        ImGui::BeginDisabled(allInstalled);
        if (UIWidgets::Button(GameEngine::VoiceInstallLabel())) {
            ImGui::OpenPopup("JP/EU Audio");
        }
        ImGui::EndDisabled();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Uses the JP and EU ROMs in /switch/starship/ (64 MB free needed); "
                            "English is already in your US game.");
        ImGui::PopTextWrapPos();
    }

    if (ImGui::BeginPopupModal("JP/EU Audio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (const int result = GameEngine::TakeVoiceInstallResult()) {
            sInstallMessage =
                result == 1   ? "Audio installed. Choose it under Voices, then restart Starship."
                : result == 2 ? "Not enough space on the SD card, or it could not be written.\n"
                                "The install was canceled and its files were removed."
                : result == 5 ? "No JP or EU ROM to install was found in /switch/starship/."
                              : "The install failed and its files were removed.";
        }
        if (sInstallMessage != nullptr) {
            ImGui::TextUnformatted(sInstallMessage);
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(360, 0))) {
                sInstallMessage = nullptr;
                ImGui::CloseCurrentPopup();
            }
        } else if (GameEngine::IsVoiceInstallPending()) {
            ImGui::TextUnformatted("Installing JP/EU audio...");
            ImGui::Spacing();
            ImGui::TextUnformatted("This can take a minute or more.");
            ImGui::TextUnformatted("The screen will stay frozen until it is done.");
            ImGui::TextUnformatted("Do not close the game.");
        } else {
            ImGui::TextUnformatted("Install JP/EU audio?");
            ImGui::Spacing();
            ImGui::TextUnformatted("This can take a minute or more, and the screen");
            ImGui::TextUnformatted("will freeze until it is done. Do not close the game.");
            ImGui::Spacing();
            if (ImGui::Button("Install", ImVec2(360, 0))) {
                GameEngine::RequestVoiceInstall();
            }
            if (ImGui::Button("Cancel", ImVec2(360, 0))) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}
#pragma pop_macro("BeginCombo")
#endif

void DrawSettingsMenu(){
    if(UIWidgets::BeginMenu("Settings")){
        if (UIWidgets::BeginMenu("Audio")) {
            UIWidgets::CVarSliderFloat("Master Volume", "gGameMasterVolume", 0.0f, 1.0f, 1.0f, {
                .format = "%.0f%%",
                .isPercentage = true,
            });
            if (UIWidgets::CVarSliderFloat("Main Music Volume", "gMainMusicVolume", 0.0f, 1.0f, 1.0f, {
                .format = "%.0f%%",
                .isPercentage = true,
            })) {
                float val = VolumeSliderToGame(CVarGetFloat("gMainMusicVolume", 1.0f));
                gSaveFile.save.data.musicVolume = (u8) val;
                Audio_SetVolume(AUDIO_TYPE_MUSIC, (u8) val);
            }
            if (UIWidgets::CVarSliderFloat("Voice Volume", "gVoiceVolume", 0.0f, 1.0f, 1.0f, {
                .format = "%.0f%%",
                .isPercentage = true,
            })) {
                float val = VolumeSliderToGame(CVarGetFloat("gVoiceVolume", 1.0f));
                gSaveFile.save.data.voiceVolume = (u8) val;
                Audio_SetVolume(AUDIO_TYPE_VOICE, (u8) val);
            }
            if (UIWidgets::CVarSliderFloat("Sound Effects Volume", "gSFXMusicVolume", 0.0f, 1.0f, 1.0f, {
                .format = "%.0f%%",
                .isPercentage = true,
            })) {
                float val = VolumeSliderToGame(CVarGetFloat("gSFXMusicVolume", 1.0f));
                gSaveFile.save.data.sfxVolume = (u8) val;
                Audio_SetVolume(AUDIO_TYPE_SFX, (u8) val);
            }

            static std::unordered_map<Ship::AudioBackend, const char*> audioBackendNames = {
                    { Ship::AudioBackend::WASAPI, "Windows Audio Session API" },
                    { Ship::AudioBackend::SDL, "SDL" },
            };

            ImGui::Text("Audio API (Needs reload)");
            auto currentAudioBackend = Ship::Context::GetInstance()->GetAudio()->GetCurrentAudioBackend();

            if (Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->size() <= 1) {
                UIWidgets::DisableComponent(ImGui::GetStyle().Alpha * 0.5f);
            }
            if (ImGui::BeginCombo("##AApi", audioBackendNames[currentAudioBackend])) {
                for (uint8_t i = 0; i < Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->size(); i++) {
                    auto backend = Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->data()[i];
                    if (ImGui::Selectable(audioBackendNames[backend], backend == currentAudioBackend)) {
                        Ship::Context::GetInstance()->GetAudio()->SetCurrentAudioBackend(backend);
                    }
                }
                ImGui::EndCombo();
            }
            if (Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->size() <= 1) {
                UIWidgets::ReEnableComponent("");
            }
            
            UIWidgets::PaddedEnhancementCheckbox("Surround 5.1 (Needs reload)", "gAudioChannelsSetting", 1, 0);
            
            if (CVarGetInteger("gAudioChannelsSetting", 0) == 1) {
                // Subwoofer threshold
                UIWidgets::CVarSliderInt("Subwoofer threshold (Hz)", "gSubwooferThreshold", 10u, 1000u, 80u, {
                    .tooltip = "The threshold for the subwoofer to be activated. Any sound under this frequency will be played on the subwoofer.",
                    .format = "%d",
                });

                // Rear music volume slider
                UIWidgets::CVarSliderFloat("Rear music volume", "gVolumeRearMusic", 0.0f, 1.0f, 1.0f, {
                    .format = "%.0f%%",
                    .isPercentage = true,
                });

                // Configurable positioning of speakers
                DrawSpeakerPositionEditor();
            }

            ImGui::EndMenu();
        }
        
        // On Switch this stays reachable, so the install status is always visible.
#ifndef __SWITCH__
        if (!GameEngine::HasVersion(SF64_VER_JP) || GameEngine::HasVersion(SF64_VER_EU))
#endif
        {
            UIWidgets::Spacer(0);
            if (UIWidgets::BeginMenu("Language")) {
                ImGui::Dummy(ImVec2(150.0f * kMenuWidthScale, 0.0f));
#ifndef __SWITCH__
                if (!GameEngine::HasVersion(SF64_VER_JP) && (GameEngine::HasVersion(SF64_VER_EU) || GameEngine::HasVersion(SF64_VER_EU_SPA))) {
                    if (GameEngine::HasVersion(SF64_VER_EU_SPA)) {
                        //UIWidgets::Spacer(0);
                        if (UIWidgets::CVarCombobox("Voices", "gVoiceLanguage", voiceLangsSPA, 
                            {
                                .tooltip = "Changes the language of the voice acting in the game",
                                .defaultIndex = 0,
                            })) {
                                Audio_SetVoiceLanguage(CVarGetInteger("gVoiceLanguage", 0));
                            };
                    } else {
                        //UIWidgets::Spacer(0);
                        if (UIWidgets::CVarCombobox("Voices", "gVoiceLanguage", voiceLangs, 
                            {
                                .tooltip = "Changes the language of the voice acting in the game",
                                .defaultIndex = 0,
                            })) {
                                Audio_SetVoiceLanguage(CVarGetInteger("gVoiceLanguage", 0));
                            };
                    }
                }
                else
#endif
                {
#ifdef __SWITCH__
                    DrawVoiceInstallControls();
#else
                    if (UIWidgets::Button("Install JP/EU Audio")) {
                        if (GameEngine::GenAssetFile(false)){
                            GameEngine::ShowMessage("Success", "Audio assets installed. Changes will be applied on the next startup.", SDL_MESSAGEBOX_INFORMATION);
                        }
                        Ship::Context::GetInstance()->GetWindow()->Close();
                    }
#endif
                }
                ImGui::EndMenu();
            }
        }
        
        UIWidgets::Spacer(0);

        if (UIWidgets::BeginMenu("Controller")) {
            UIWidgets::WindowButton("Controller Mapping", "gInputEditorWindow", GameUI::mInputEditorWindow);

            UIWidgets::Spacer(0);

            UIWidgets::CVarCheckbox("Menubar Controller Navigation", "gControlNav", {
                .tooltip = "Allows controller navigation of the SOH menu bar (Settings, Enhancements,...)\nCAUTION: This will disable game inputs while the menubar is visible.\n\nD-pad to move between items, A to select, and X to grab focus on the menu bar"
            });

            UIWidgets::CVarCheckbox("Invert Y Axis", "gInvertYAxis",{
                .tooltip = "Inverts the Y axis for controlling vehicles"
            });

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

#ifndef __SWITCH2__
    ImGui::SetCursorPosY(0.0f);
#endif
    if (UIWidgets::BeginMenu("Graphics")) {
        UIWidgets::WindowButton("Resolution Editor", "gAdvancedResolutionEditorEnabled", GameUI::mAdvancedResolutionSettingsWindow);

        UIWidgets::Spacer(0);

        // Previously was running every frame, and nothing was setting it? Maybe a bad copy/paste?
        // Ship::Context::GetInstance()->GetWindow()->SetResolutionMultiplier(CVarGetFloat("gInternalResolution", 1));
        // UIWidgets::Tooltip("Multiplies your output resolution by the value inputted, as a more intensive but effective form of anti-aliasing");
#ifndef __WIIU__
        if (UIWidgets::CVarSliderInt("MSAA: %d", "gMSAAValue", 1, 8, 1, {
            .tooltip = "Activates multi-sample anti-aliasing when above 1x up to 8x for 8 samples for every pixel"
        })) {
            Ship::Context::GetInstance()->GetWindow()->SetMsaaLevel(CVarGetInteger("gMSAAValue", 1));
        }
#endif

        { // FPS Slider
            const int minFps = 30;
            static int maxFps = 360;
            int currentFps = 0;
        #ifdef __WIIU__
            UIWidgets::Spacer(0);
            // only support divisors of 60 on the Wii U
            if (currentFps > 60) {
                currentFps = 60;
            } else {
                currentFps = 60 / (60 / currentFps);
            }

            int fpsSlider = 1;
            if (currentFps == 30) {
                ImGui::Text("FPS: Original (30)");
            } else {
                ImGui::Text("FPS: %d", currentFps);
                if (currentFps == 30) {
                    fpsSlider = 2;
                } else { // currentFps == 60
                    fpsSlider = 3;
                }
            }
            if (CVarGetInteger("gMatchRefreshRate", 0)) {
                UIWidgets::DisableComponent(ImGui::GetStyle().Alpha * 0.5f);
            }

            if (ImGui::Button(" - ##WiiUFPS")) {
                fpsSlider--;
            }
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 7.0f);

            UIWidgets::Spacer(0);

            ImGui::PushItemWidth(std::min((ImGui::GetContentRegionAvail().x - 60.0f), 260.0f));
            ImGui::SliderInt("##WiiUFPSSlider", &fpsSlider, 1, 3, "", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 7.0f);
            if (ImGui::Button(" + ##WiiUFPS")) {
                fpsSlider++;
            }

            if (CVarGetInteger("gMatchRefreshRate", 0)) {
                UIWidgets::ReEnableComponent("");
            }
            if (fpsSlider > 3) {
                fpsSlider = 3;
            } else if (fpsSlider < 1) {
                fpsSlider = 1;
            }

            if (fpsSlider == 1) {
                currentFps = 20;
            } else if (fpsSlider == 2) {
                currentFps = 30;
            } else if (fpsSlider == 3) {
                currentFps = 60;
            }
            CVarSetInteger("gInterpolationFPS", currentFps);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        #else
            bool matchingRefreshRate = CVarGetInteger("gMatchRefreshRate", 0);
            UIWidgets::CVarSliderInt((currentFps == 30) ? "FPS: Original (30)" : "FPS: %d", "gInterpolationFPS", minFps, maxFps, 60, {
                .disabled = matchingRefreshRate
            });
        #endif
            UIWidgets::Tooltip(
                "Uses Matrix Interpolation to create extra frames, resulting in smoother graphics. This is purely "
                "visual and does not impact game logic, execution of glitches etc.\n\n"
                "A higher target FPS than your monitor's refresh rate will waste resources, and might give a worse result."
            );
        } // END FPS Slider

        UIWidgets::PaddedEnhancementCheckbox("Match Refresh Rate", "gMatchRefreshRate", true, false);
        UIWidgets::Tooltip("Matches interpolation value to the refresh rate of your display.");

        if (Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() == Ship::WindowBackend::FAST3D_DXGI_DX11) {
            UIWidgets::PaddedEnhancementCheckbox("Render parallelization","gRenderParallelization", true, false, {}, {}, {}, true);
            UIWidgets::Tooltip(
                "This setting allows the CPU to work on one frame while GPU works on the previous frame.\n"
                "Recommended if you can't reach the FPS you set, despite it being set below your refresh rate "
                "or if you notice other performance problems.\n"
                "Adds up to one frame of input lag under certain scenarios.");
        }
      
        UIWidgets::PaddedSeparator(true, true, 3.0f, 3.0f);

        static std::unordered_map<Ship::WindowBackend, const char*> windowBackendNames = {
                { Ship::WindowBackend::FAST3D_DXGI_DX11, "DirectX" },
                { Ship::WindowBackend::FAST3D_SDL_OPENGL, "OpenGL"},
                { Ship::WindowBackend::FAST3D_SDL_METAL, "Metal" }
        };

        ImGui::Text("Renderer API (Needs reload)");
        Ship::WindowBackend runningWindowBackend = Ship::Context::GetInstance()->GetWindow()->GetWindowBackend();
        Ship::WindowBackend configWindowBackend;
        int configWindowBackendId = Ship::Context::GetInstance()->GetConfig()->GetInt("Window.Backend.Id", -1);
        if (configWindowBackendId != -1 && configWindowBackendId < static_cast<int>(Ship::WindowBackend::WINDOW_BACKEND_COUNT)) {
            configWindowBackend = static_cast<Ship::WindowBackend>(configWindowBackendId);
        } else {
            configWindowBackend = runningWindowBackend;
        }

        if (Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends()->size() <= 1) {
            UIWidgets::DisableComponent(ImGui::GetStyle().Alpha * 0.5f);
        }
        if (ImGui::BeginCombo("##RApi", windowBackendNames[configWindowBackend])) {
            for (size_t i = 0; i < Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends()->size(); i++) {
                auto backend = Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends()->data()[i];
                if (ImGui::Selectable(windowBackendNames[backend], backend == configWindowBackend)) {
                    Ship::Context::GetInstance()->GetConfig()->SetInt("Window.Backend.Id", static_cast<int>(backend));
                    Ship::Context::GetInstance()->GetConfig()->SetString("Window.Backend.Name",
                                                                        windowBackendNames[backend]);
                    Ship::Context::GetInstance()->GetConfig()->Save();
                }
            }
            ImGui::EndCombo();
        }
        if (Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends()->size() <= 1) {
            UIWidgets::ReEnableComponent("");
        }

        if (Ship::Context::GetInstance()->GetWindow()->CanDisableVerticalSync()) {
            UIWidgets::PaddedEnhancementCheckbox("Enable Vsync", "gVsyncEnabled", true, false, false, "", UIWidgets::CheckboxGraphics::Cross, true);
            UIWidgets::Tooltip("Removes tearing, but clamps your max FPS to your displays refresh rate.");
        }

        if (Ship::Context::GetInstance()->GetWindow()->SupportsWindowedFullscreen()) {
            UIWidgets::PaddedEnhancementCheckbox("Windowed fullscreen", "gSdlWindowedFullscreen", true, false);
        }

        if (Ship::Context::GetInstance()->GetWindow()->GetGui()->SupportsViewports()) {
            UIWidgets::PaddedEnhancementCheckbox("Allow multi-windows", "gEnableMultiViewports", true, false, false, "", UIWidgets::CheckboxGraphics::Cross, true);
            UIWidgets::Tooltip("Allows windows to be able to be dragged off of the main game window. Requires a reload to take effect.");
        }

        UIWidgets::PaddedEnhancementCheckbox("Enable Alternative Assets", "gEnhancements.Mods.AlternateAssets");
        // If more filters are added to LUS, make sure to add them to the filters list here
        ImGui::Text("Texture Filter (Needs reload)");
        UIWidgets::EnhancementCombobox("gTextureFilter", filters, 0);

        UIWidgets::PaddedEnhancementCheckbox("Apply Point Filtering to UI Elements", "gHUDPointFiltering", true, false, false, "", UIWidgets::CheckboxGraphics::Cross, true);
        UIWidgets::Spacer(0);

#ifdef __SWITCH2__
        if (!StarshipMenu::sFiltering)
#endif
        Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGameOverlay()->DrawSettings();

        ImGui::EndMenu();
    }
}

void DrawMenuBarIcon() {
    static bool gameIconLoaded = false;
    if (!gameIconLoaded) {
        // Ship::Context::GetInstance()->GetWindow()->GetGui()->LoadGuiTexture("Game_Icon", "textures/icons/gIcon.png", ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        gameIconLoaded = false;
    }

    if (Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName("Game_Icon")) {
#ifdef __SWITCH__
        ImVec2 iconSize = ImVec2(20.0f, 20.0f);
        float posScale = 1.0f;
#elif defined(__WIIU__)
        ImVec2 iconSize = ImVec2(16.0f * 2, 16.0f * 2);
        float posScale = 2.0f;
#else
        ImVec2 iconSize = ImVec2(20.0f, 20.0f);
        float posScale = 1.5f;
#endif
        ImGui::SetCursorPos(ImVec2(5, 2.5f) * posScale);
        ImGui::Image(Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName("Game_Icon"), iconSize);
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(25, 0) * posScale);
    }
}

void DrawGameMenu() {
    if (UIWidgets::BeginMenu("Starship")) {
        if (UIWidgets::MenuItem("Reset", "F4")) {
            gNextGameState = GSTATE_BOOT;
        }
#if !defined(__SWITCH__) && !defined(__WIIU__)

        if (UIWidgets::MenuItem("Toggle Fullscreen", "F11")) {
            Ship::Context::GetInstance()->GetWindow()->ToggleFullscreen();
        }
#endif

        if (UIWidgets::MenuItem("Quit Game")) {
            Ship::Context::GetInstance()->GetWindow()->Close();
        }
        ImGui::EndMenu();
    }
}

static const char* hudAspects[] = {
    "Expand", "Custom", "Original (4:3)", "Widescreen (16:9)", "Nintendo 3DS (5:3)", "16:10 (8:5)", "Ultrawide (21:9)"
};

static const char* radioCommBox[] = {
    "Original", "Expand"
};

void DrawEnhancementsMenu() {
    if (UIWidgets::BeginMenu("Enhancements")) {

        if (UIWidgets::BeginMenu("Gameplay")) {
            UIWidgets::CVarCheckbox("No Level of Detail (LOD)", "gDisableLOD", {
                .tooltip = "Disable Level of Detail (LOD) to avoid models using lower poly versions at a distance",
                .defaultValue = true
            });
            UIWidgets::CVarCheckbox("Character heads inside Arwings at all times", "gTeamFaces", {
                .tooltip = "Character heads are displayed inside Arwings in all cutscenes",
                .defaultValue = true
            });
            UIWidgets::CVarCheckbox("Use red radio backgrounds for enemies.", "gEnemyRedRadio");
            UIWidgets::CVarSliderInt("Cockpit Glass Opacity: %d", "gCockpitOpacity", 0, 255, 120);
            

            ImGui::EndMenu();
        }
        
        if (UIWidgets::BeginMenu("Fixes")) {
            UIWidgets::CVarCheckbox("Macbeth: Level ending cutscene camera fix", "gMaCameraFix", {
                .tooltip = "Fixes a camera bug found in the code of the game"
            });

            UIWidgets::CVarCheckbox("Sector Z: Spawn all actors", "gSzActorFix", {
                .tooltip = "Fixes a bug found in Sector Z, where only 10 of 12 available actors are spawned, this causes two 'Space Junk Boxes' to be missing from the level."
            });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Restoration")) {
            UIWidgets::CVarCheckbox("Sector Z: Missile cutscene bug", "gSzMissileBug", {
                .tooltip = "Restores the missile cutscene bug present in JP 1.0"
            });

            UIWidgets::CVarCheckbox("Beta: Restore beta bomb explosion", "gRestoreBetaBombExplosion", {
                .tooltip = "Restores the beta bomb explosion found inside the game"
            });

            UIWidgets::CVarCheckbox("Beta: Restore beta coin", "gRestoreBetaCoin", {
                .tooltip = "Restores the beta coin that got replaced with the gold ring"
            });

            UIWidgets::CVarCheckbox("Beta: Restore beta boost/brake gauge", "gRestoreBetaBoostGauge", {
                .tooltip = "Restores the beta boost gauge that was seen in some beta footage"
            });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("HUD")) {
            if (UIWidgets::CVarCombobox("Radio Communication Box", "gRadioCommBox.Selection", radioCommBox, 
            {
                .tooltip = "Which Aspect Ratio to use when drawing the Radio Communication Box",
                .defaultIndex = 0,
            })) {
                switch (CVarGetInteger("gRadioCommBox.Selection", 0)) {
                    case 0:
                        CVarSetInteger("gRadioCommBox.expand", 0);
                        break;
                    case 1:
                        CVarSetInteger("gRadioCommBox.expand", 1);
                        break;
                }
            }

            if (UIWidgets::CVarCombobox("HUD Aspect Ratio", "gHUDAspectRatio.Selection", hudAspects, 
            {
                .tooltip = "Which Aspect Ratio to use when drawing the HUD (Radar, gauges and radio messages)",
                .defaultIndex = 0,
            })) {
                CVarSetInteger("gHUDAspectRatio.Enabled", 1);
                switch (CVarGetInteger("gHUDAspectRatio.Selection", 0)) {
                    case 0:
                        CVarSetInteger("gHUDAspectRatio.Enabled", 0);
                        CVarSetInteger("gHUDAspectRatio.X", 0);
                        CVarSetInteger("gHUDAspectRatio.Y", 0);
                        break;
                    case 1:
                        if (CVarGetInteger("gHUDAspectRatio.X", 0) <= 0){
                            CVarSetInteger("gHUDAspectRatio.X", 1);
                        }
                        if (CVarGetInteger("gHUDAspectRatio.Y", 0) <= 0){
                            CVarSetInteger("gHUDAspectRatio.Y", 1);
                        }
                        break;
                    case 2:
                        CVarSetInteger("gHUDAspectRatio.X", 4);
                        CVarSetInteger("gHUDAspectRatio.Y", 3);
                        break;
                    case 3:
                        CVarSetInteger("gHUDAspectRatio.X", 16);
                        CVarSetInteger("gHUDAspectRatio.Y", 9);
                        break;
                    case 4:
                        CVarSetInteger("gHUDAspectRatio.X", 5);
                        CVarSetInteger("gHUDAspectRatio.Y", 3);
                        break;
                    case 5:
                        CVarSetInteger("gHUDAspectRatio.X", 8);
                        CVarSetInteger("gHUDAspectRatio.Y", 5);
                        break;
                    case 6:
                        CVarSetInteger("gHUDAspectRatio.X", 21);
                        CVarSetInteger("gHUDAspectRatio.Y", 9);
                        break;                    
                }
            }
            
            if (CVarGetInteger("gHUDAspectRatio.Selection", 0) == 1)
            {
                UIWidgets::CVarSliderInt("Horizontal: %d", "gHUDAspectRatio.X", 1, 100, 1);
                UIWidgets::CVarSliderInt("Vertical: %d", "gHUDAspectRatio.Y", 1, 100, 1);
            }

            ImGui::Dummy(ImVec2(ImGui::CalcTextSize("Nintendo 3DS (5:3)").x + 35, 0.0f));
            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Accessibility")) { 
            UIWidgets::CVarCheckbox("Disable Gorgon (Area 6 boss) screen flashes", "gDisableGorgonFlash", {
                .tooltip = "Gorgon flashes the screen repeatedly when firing its beam or when teleporting, which causes eye pain for some players and may be harmful to those with photosensitivity.",
                .defaultValue = false
            });
            UIWidgets::CVarCheckbox("Add outline to Arwing and Wolfen in radar", "gFighterOutlines", {
                .tooltip = "Increases visibility of ships in the radar.",
                .defaultValue = false
            });
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }
}

void DrawCheatsMenu() {
    if (UIWidgets::BeginMenu("Cheats")) {
        UIWidgets::CVarCheckbox("Infinite Lives", "gInfiniteLives");
        UIWidgets::CVarCheckbox("Invincible", "gInvincible");
        UIWidgets::CVarCheckbox("Unbreakable Wings", "gUnbreakableWings");
        UIWidgets::CVarCheckbox("Infinite Bombs", "gInfiniteBombs");
        UIWidgets::CVarCheckbox("Infinite Boost/Brake", "gInfiniteBoost");
        UIWidgets::CVarCheckbox("Hyper Laser", "gHyperLaser");
        UIWidgets::CVarSliderInt("Laser Range Multiplier: %d%%", "gLaserRangeMult", 15, 800, 100,
            { .tooltip = "Changes how far your lasers fly." });
        UIWidgets::CVarCheckbox("Rapid-fire mode", "gRapidFire", {
                .tooltip = "Hold A to keep firing. Release A to start charging a shot."
            });
            if (CVarGetInteger("gRapidFire", 0) == 1) {
                ImGui::Dummy(ImVec2(22.0f * kMenuWidthScale, 0.0f));
                ImGui::SameLine();
                UIWidgets::CVarCheckbox("Hold L to Charge", "gLtoCharge", {
                    .tooltip = "If you prefer to not have auto-charge."
                });
            }
        UIWidgets::CVarCheckbox("Self destruct button", "gHit64SelfDestruct", {
                .tooltip = "Press Down on the D-PAD to instantly self destruct."
            });
        UIWidgets::CVarCheckbox("Start with Falco dead", "gHit64FalcoDead", {
                .tooltip = "Start the level with with Falco dead."
            });
        UIWidgets::CVarCheckbox("Start with Slippy dead", "gHit64SlippyDead", {
                .tooltip = "Start the level with with Slippy dead."
            });
        UIWidgets::CVarCheckbox("Start with Peppy dead", "gHit64PeppyDead", {
                .tooltip = "Start the level with with Peppy dead."
            });
        
        UIWidgets::CVarCheckbox("Score Editor", "gScoreEditor", { .tooltip = "Enable the score editor" });

        if (CVarGetInteger("gScoreEditor", 0) == 1) {
            UIWidgets::CVarSliderInt("Score: %d", "gScoreEditValue", 0, 999, 0,
                { .tooltip = "Increase or decrease the current mission score number" });
        }

        ImGui::EndMenu();
    }
}

static const char* debugInfoPages[6] = {
    "Object",
    "Check Surface",
    "Map",
    "Stage",
    "Effect",
    "Enemy",
};

static const char* logLevels[] = {
    "trace", "debug", "info", "warn", "error", "critical", "off",
};

void DrawDebugMenu() {
    if (UIWidgets::BeginMenu("Developer")) {
        if (UIWidgets::CVarCombobox("Log Level", "gDeveloperTools.LogLevel", logLevels, {
            .tooltip = "The log level determines which messages are printed to the "
                        "console. This does not affect the log file output",
            .defaultIndex = 1,
        })) {
            Ship::Context::GetInstance()->GetLogger()->set_level((spdlog::level::level_enum)CVarGetInteger("gDeveloperTools.LogLevel", 1));
        }

#if defined(__SWITCH__) && !defined(__SWITCH2__)
        if (UIWidgets::CVarCombobox("Switch CPU Profile", "gSwitchPerfMode", SWITCH_CPU_PROFILES, {
            .tooltip = "Switches the CPU profile to a different one",
            .defaultIndex = (int)Ship::SwitchProfiles::STOCK
        })) {
            SPDLOG_INFO("Profile:: %s", SWITCH_CPU_PROFILES[CVarGetInteger("gSwitchPerfMode", (int)Ship::SwitchProfiles::STOCK)]);
            Ship::Switch::ApplyOverclock();
        }
#endif

        UIWidgets::WindowButton("Gfx Debugger", "gGfxDebuggerEnabled", GameUI::mGfxDebuggerWindow, {
            .tooltip = "Enables the Gfx Debugger window, allowing you to input commands, type help for some examples"
        });

        // UIWidgets::CVarCheckbox("Debug mode", "gEnableDebugMode", {
        //     .tooltip = "TBD"
        // });

        UIWidgets::CVarCheckbox("Level Selector", "gLevelSelector", {
            .tooltip = "Allows you to select any level from the main menu"
        });

        UIWidgets::CVarCheckbox("Skip Briefing", "gSkipBriefing", {
            .tooltip = "Allows you to skip the briefing sequence in level select"
        });

        UIWidgets::CVarCheckbox("Enable Expert Mode", "gForceExpertMode", {
            .tooltip = "Allows you to force expert mode"
        });

        UIWidgets::CVarCheckbox("SFX Jukebox", "gSfxJukebox", {
            .tooltip = "Press L in the Expert Sound options to play sound effects from the game"
        });

        UIWidgets::CVarCheckbox("Disable Starfield interpolation", "gDisableStarsInterpolation", {
            .tooltip = "Disable starfield interpolation to increase performance on slower CPUs"
        });
        UIWidgets::CVarCheckbox("Disable Gamma Boost (Needs reload)", "gGraphics.GammaMode", {
            .tooltip = "Disables the game's Built-in Gamma Boost. Useful for modders",
            .defaultValue = false
        });

        UIWidgets::CVarCheckbox("Spawner Mod", "gSpawnerMod", {
            .tooltip = "Spawn Scenery, Actors, Bosses, Sprites, Items, Effects and even Event Actors.\n"
                       "\n"
                       "Controls:\n"
                       "D-Pad left and right to set the object Id.\n"
                       "C-Right to change between spawn modes.\n"
                       "Analog stick sets the spawn position.\n"
                       "L-Trigger to spawn the object.\n"
                       "D-Pad UP to kill all objects.\n"
                       "D-Pad DOWN to freeze/unfreeze the ship speed.\n"
                       "WARNING: Spawning an object that's not loaded in memory will likely result in a crash."
        });

        UIWidgets::CVarCheckbox("Jump To Map", "gDebugJumpToMap", {
            .tooltip = "Press Z + R + C-UP to get back to the map"
        });

        UIWidgets::CVarCheckbox("L To Warp Zone", "gDebugWarpZone", {
            .tooltip = "Press L to get into the Warp Zone"
        });

        UIWidgets::CVarCheckbox("L to Level Complete", "gDebugLevelComplete", {
            .tooltip = "Press L to Level Complete"
        });

        UIWidgets::CVarCheckbox("L to All-Range mode", "gDebugJumpToAllRange", {
            .tooltip = "Press L to switch to All-Range mode"
        });

        UIWidgets::CVarCheckbox("Disable Collision", "gDebugNoCollision", {
            .tooltip = "Disable vehicle collision"
        });
        
        UIWidgets::CVarCheckbox("Speed Control", "gDebugSpeedControl", {
            .tooltip = "Arwing speed control. Use D-PAD Left and Right to Increase/Decrease the Arwing Speed, D-PAD Down to stop movement."
        });

        UIWidgets::CVarCheckbox("Debug Ending", "gDebugEnding", {
            .tooltip = "Jump to credits at the main menu"
        });

        UIWidgets::CVarCheckbox("Debug Pause", "gLToDebugPause", {
            .tooltip = "Press L to toggle Debug Pause"
        });
        if (CVarGetInteger("gLToDebugPause", 0)) {
            ImGui::Dummy(ImVec2(22.0f * kMenuWidthScale, 0.0f));
            ImGui::SameLine();
            UIWidgets::CVarCheckbox("Frame Advance", "gLToFrameAdvance", {
            .tooltip = "Pressing L again advances one frame instead"
        });
        }

        if (CVarGetInteger(StringHelper::Sprintf("gCheckpoint.%d.Set", gCurrentLevel).c_str(), 0)) {
            if (
#ifdef __SWITCH2__
                UIWidgets::StarshipShow("Clear Checkpoint") &&
#endif
                UIWidgets::Button("Clear Checkpoint")) {
                CVarClear(StringHelper::Sprintf("gCheckpoint.%d.Set", gCurrentLevel).c_str());
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        } else if (gPlayer != NULL && gGameState == GSTATE_PLAY) {
            if (
#ifdef __SWITCH2__
                UIWidgets::StarshipShow("Set Checkpoint") &&
#endif
                UIWidgets::Button("Set Checkpoint")) {
                CVarSetInteger(StringHelper::Sprintf("gCheckpoint.%d.Set", gCurrentLevel).c_str(), 1);
                CVarSetInteger(StringHelper::Sprintf("gCheckpoint.%d.gSavedPathProgress", gCurrentLevel).c_str(), gGroundSurface);
                CVarSetFloat(StringHelper::Sprintf("gCheckpoint.%d.gSavedPathProgress", gCurrentLevel).c_str(), (-gPlayer->pos.z) - 250.0f);
                CVarSetInteger(StringHelper::Sprintf("gCheckpoint.%d.gSavedObjectLoadIndex", gCurrentLevel).c_str(), gObjectLoadIndex);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        }

        UIWidgets::Spacer(0);

        UIWidgets::WindowButton("Stats", "gStatsEnabled", GameUI::mStatsWindow, {
            .tooltip = "Shows the stats window, with your FPS and frametimes, and the OS you're playing on"
        });
        UIWidgets::WindowButton("Console", "gConsoleEnabled", GameUI::mConsoleWindow, {
            .tooltip = "Enables the console window, allowing you to input commands, type help for some examples"
        });

        ImGui::EndMenu();
    }
}

#ifdef __SWITCH2__
static bool StarshipHeaderButton(const char* label, bool selected) {
    const ImVec4 accent(0.16f, 0.45f, 0.78f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? accent : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent.x, accent.y, accent.z, 0.6f));
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    return pressed;
}

static bool StarshipRedButton(const char* icon) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.14f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.14f, 0.14f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.14f, 0.14f, 0.6f));
    const bool pressed = ImGui::Button(icon);
    ImGui::PopStyleColor(3);
    return pressed;
}

static void DrawStarshipMenu(GameMenuBar* menu) {
    using namespace StarshipMenu;
    static int sLastFrame = -2;
    if (ImGui::GetFrameCount() != sLastFrame + 1) {
        ImGui::SetNextWindowFocus();
    }
    sLastFrame = ImGui::GetFrameCount();

    sSections = sNextSections;
    sNextSections.clear();
    const auto& sections = sSections[sTab];
    if (!sections.empty() && std::find(sections.begin(), sections.end(), sSection) == sections.end()) {
        sSection = sections.front();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
    ImGui::Begin("Starship Menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 blockSize(std::min(viewport->Size.x * 0.9f, viewport->Size.y * 1.77f), viewport->Size.y * 0.9f);
    const ImVec2 blockPos(viewport->Pos.x + (viewport->Size.x - blockSize.x) / 2,
                          viewport->Pos.y + (viewport->Size.y - blockSize.y) / 2);
    ImGui::SetCursorScreenPos(blockPos);

    static const char* kTabs[] = { "Settings", "Enhancements", "Cheats", "Developer" };
    for (const char* tab : kTabs) {
        if (StarshipHeaderButton(tab, sTab == tab)) {
            sTab = tab;
            sSection.clear();
        }
        ImGui::SameLine();
    }
    const float iconWidth = ImGui::CalcTextSize(ICON_FA_TIMES_CIRCLE).x + style.FramePadding.x * 2;
    const float iconsX = blockPos.x + blockSize.x - iconWidth * 3 - style.ItemSpacing.x * 2;
    const float searchX = ImGui::GetCursorScreenPos().x;
    const float minSearch = ImGui::CalcTextSize("Search...").x + style.FramePadding.x * 2 + 8.0f;
    const float searchWidth = std::max(minSearch, std::min(iconsX - searchX - style.ItemSpacing.x, 400.0f));
    ImGui::SetCursorScreenPos(ImVec2(searchX, blockPos.y));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.45f, 0.78f, 0.6f));
    sSearch.Draw("##StarshipSearch", searchWidth);
    ImGui::PopStyleColor();
    sSearchText = Normalized(sSearch.InputBuf);
    if (sSearch.InputBuf[0] == '\0' && !ImGui::IsItemActive()) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(searchX + style.FramePadding.x, blockPos.y + style.FramePadding.y),
                                            IM_COL32(255, 255, 255, 110), "Search...");
    }
    ImGui::SetCursorScreenPos(ImVec2(blockPos.x + blockSize.x - iconWidth * 3 - style.ItemSpacing.x * 2, blockPos.y));
    if (StarshipRedButton(ICON_FA_POWER_OFF "##quit")) {
        ImGui::OpenPopup("Quit Game");
    }
    ImGui::SameLine();
    if (StarshipRedButton(ICON_FA_REFRESH "##reset")) {
        gNextGameState = GSTATE_BOOT;
    }
    ImGui::SameLine();
    if (StarshipRedButton(ICON_FA_TIMES_CIRCLE "##close")) {
        menu->ToggleVisibility();
    }

    if (ImGui::BeginPopupModal("Quit Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Are you sure you want to quit?");
        if (ImGui::Button("Quit Game##confirm")) {
            ImGui::CloseCurrentPopup();
            Ship::Context::GetInstance()->GetWindow()->Close();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##quit")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const float headerHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y;
    const float lineY = blockPos.y + headerHeight;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(blockPos.x, lineY), ImVec2(blockPos.x + blockSize.x, lineY + 4.0f), IM_COL32_WHITE);
    const float bodyY = lineY + 4.0f + style.ItemSpacing.y;
    const float bodyHeight = blockPos.y + blockSize.y - bodyY;

    float contentX = blockPos.x;
    if (!sections.empty() && sSearchText.empty()) {
        float sidebarWidth = 0.0f;
        for (const auto& section : sections) {
            sidebarWidth = std::max(sidebarWidth, ImGui::CalcTextSize(section.c_str()).x);
        }
        sidebarWidth += style.FramePadding.x * 2 + style.ItemSpacing.x * 2;
        ImGui::SetCursorScreenPos(ImVec2(blockPos.x, bodyY));
        ImGui::BeginChild("Starship Sections", ImVec2(sidebarWidth, bodyHeight), ImGuiChildFlags_NavFlattened);
        for (const auto& section : sections) {
            if (StarshipHeaderButton(section.c_str(), sSection == section)) {
                sSection = section;
            }
        }
        ImGui::EndChild();
        const float dividerX = blockPos.x + sidebarWidth + style.ItemSpacing.x;
        draw->AddRectFilled(ImVec2(dividerX, bodyY), ImVec2(dividerX + 4.0f, bodyY + bodyHeight), IM_COL32_WHITE);
        contentX = dividerX + 4.0f + style.ItemSpacing.x;
    }

    ImGui::SetCursorScreenPos(ImVec2(contentX, bodyY));
    ImGui::BeginChild("Starship Content", ImVec2(blockPos.x + blockSize.x - contentX, bodyHeight),
                      ImGuiChildFlags_NavFlattened);
    sFiltering = !sSearchText.empty();
    sLastShown = true;
    sMatches = 0;
    DrawSettingsMenu();
    DrawEnhancementsMenu();
    DrawCheatsMenu();
    DrawDebugMenu();
    const bool noResults = sFiltering && sMatches == 0;
    sFiltering = false;
    if (noResults) {
        ImGui::TextUnformatted("No results found");
    }
    ImGui::EndChild();
    ImGui::End();
}
#endif

void GameMenuBar::DrawElement() {
#ifdef __SWITCH2__
    DrawStarshipMenu(this);
    return;
#endif
    if(ImGui::BeginMenuBar()){
        DrawMenuBarIcon();

        DrawGameMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawSettingsMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawEnhancementsMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawCheatsMenu();

        ImGui::SetCursorPosY(0.0f);

        ImGui::SetCursorPosY(0.0f);

        DrawDebugMenu();

        ImGui::EndMenuBar();
    }
}