#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "port/notification/notification.h"
#include "port/switch/Switch2Debug.h"
#include "port/switch/SwitchLoading.h"
#ifdef __SWITCH__
#include "port/switch/SwitchShaderCache.h"
#endif
#include "Engine.h"
#include "ui/ImguiUI.h"
#include "StringHelper.h"

#include "extractor/GameExtractor.h"
#include "libultraship/src/Context.h"
#include "libultraship/src/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"
#include "resource/type/ResourceType.h"
#include "resource/importers/AnimFactory.h"
#include "resource/importers/ColPolyFactory.h"
#include "resource/importers/EnvSettingsFactory.h"
#include "resource/importers/GenericArrayFactory.h"
#include "resource/importers/HitboxFactory.h"
#include "resource/importers/LimbFactory.h"
#include "resource/importers/MessageFactory.h"
#include "resource/importers/MessageLookupFactory.h"
#include "resource/importers/ObjectInitFactory.h"
#include "resource/importers/ScriptCommandFactory.h"
#include "resource/importers/ScriptFactory.h"
#include "resource/importers/SkeletonFactory.h"
#include "resource/importers/Vec3fFactory.h"
#include "resource/importers/Vec3sFactory.h"

#include "resource/importers/audio/AudioTableFactory.h"
#include "resource/importers/audio/BookFactory.h"
#include "resource/importers/audio/DrumFactory.h"
#include "resource/importers/audio/EnvelopeFactory.h"
#include "resource/importers/audio/InstrumentFactory.h"
#include "resource/importers/audio/LoopFactory.h"
#include "resource/importers/audio/SampleFactory.h"
#include "resource/importers/audio/SoundFontFactory.h"

#include "port/interpolation/FrameInterpolation.h"
#include <Fast3D/Fast3dWindow.h>
#include <DisplayListFactory.h>
#include <TextureFactory.h>
#include <MatrixFactory.h>
#include <BlobFactory.h>
#include <VertexFactory.h>
#include "audio/GameAudio.h"
#include "port/patches/DisplayListPatch.h"
#include "port/mods/PortEnhancements.h"

#include <Fast3D/interpreter.h>
#include <filesystem>

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#endif

namespace fs = std::filesystem;

extern "C" {
bool prevAltAssets = false;
bool gEnableGammaBoost = true;
#include <sf64thread.h>
#include <macros.h>
#include "sf64audio_provisional.h"
void AudioThread_CreateNextAudioBuffer(int16_t* samples, uint32_t num_samples);
}

std::vector<uint8_t*> MemoryPool;
GameEngine* GameEngine::Instance;

#ifdef __SWITCH__
namespace {
std::string VoiceGuardPath() {
    return Ship::Context::GetPathRelativeToAppDirectory("voice-check.tmp");
}

int sActiveVoicePack = 0;

// Sets aside a voice archive that is damaged, or that was active when the last
// launch died, so one bad install cannot stop the game from starting.
void CheckVoiceArchives() {
    std::error_code ec;
    std::string diedWith;
    {
        std::ifstream guard(VoiceGuardPath());
        std::getline(guard, diedWith);
    }
    for (const char* name : { "sf64jp.o2r", "sf64eu.o2r" }) {
        const auto path = std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("mods")) / name;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        const bool complete = GameExtractor::IsCompleteArchive(path);
        const bool died = diedWith == name;
        char line[160];
        snprintf(line, sizeof(line), "B boot:voice %s bytes=%llu complete=%d last-boot-died=%d", name,
                 (unsigned long long) std::filesystem::file_size(path, ec), (int) complete, (int) died);
        Switch2Debug_Log(line);
        if (complete && !died) {
            continue;
        }
        auto bad = path;
        bad += ".bad";
        std::filesystem::remove(bad, ec);
        std::filesystem::rename(path, bad, ec);
    }
    std::filesystem::remove(VoiceGuardPath(), ec);
}

// JP and EU reuse many asset names with different data, so only one can be loaded.
void SelectVoiceArchive(std::vector<std::string>& archives) {
    const auto isNamed = [](const std::string& path, const char* name) {
        return std::filesystem::path(path).filename() == name;
    };
    const bool haveJp = std::any_of(archives.begin(), archives.end(), [&](auto& p) { return isNamed(p, "sf64jp.o2r"); });
    const bool haveEu = std::any_of(archives.begin(), archives.end(), [&](auto& p) { return isNamed(p, "sf64eu.o2r"); });
    int pack = CVarGetInteger("gVoicePack", 0);
    if ((pack == 1 && !haveJp) || (pack == 2 && !haveEu)) {
        pack = 0;
    }
    sActiveVoicePack = pack;
    // The EU game holds English and Lylat voices. "European" in the menu means Lylat.
    CVarSetInteger("gVoiceLanguage", pack == 2 ? 1 : 0);
    archives.erase(std::remove_if(archives.begin(), archives.end(),
                                  [&](const std::string& path) {
                                      return (pack != 1 && isNamed(path, "sf64jp.o2r")) ||
                                             (pack != 2 && isNamed(path, "sf64eu.o2r"));
                                  }),
                   archives.end());
    char line[64];
    snprintf(line, sizeof(line), "B boot:voice-pack %d", pack);
    Switch2Debug_Log(line);
    if (pack != 0) {
        std::ofstream guard(VoiceGuardPath());
        guard << (pack == 1 ? "sf64jp.o2r" : "sf64eu.o2r") << "\n";
    }
}
} // namespace
#endif

GameEngine::GameEngine() {
    // Initialize context properties early to recognize paths properly for non-portable builds
    this->context = Ship::Context::CreateUninitializedInstance("Starship", "ship", "starship.cfg.json");

#ifdef __SWITCH__
    {
        // sf64.o2r can be made on the console from a ROM in this folder.
        bool hasRom = false;
        std::error_code romError;
        for (const auto& entry : std::filesystem::directory_iterator(".", romError)) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            hasRom = hasRom || ext == ".z64" || ext == ".n64" || ext == ".v64";
        }
        Ship::Switch::RequireGameFiles("Starship", "/switch/starship/", { "starship.o2r" },
                                       hasRom ? std::vector<std::string>{} : std::vector<std::string>{ "sf64.o2r" },
                                       "Copy sf64.o2r or your US Star Fox 64 ROM there.\n"
                                       "See GAME-DATA.md in the download.");
    }
    Ship::Switch::Init(Ship::PreInitPhase);
    Ship::Switch::Init(Ship::PostInitPhase);
    // Needs the sockets that PreInitPhase opens.
    Switch2Debug_Start();
    Switch2Debug_BootMark("start");
#endif

    std::vector<std::string> archiveFiles;
    const std::string main_path = Ship::Context::GetPathRelativeToAppDirectory("sf64.o2r");
    const std::string assets_path = Ship::Context::LocateFileAcrossAppDirs("starship.o2r");

#ifdef _WIN32
    AllocConsole();
#endif

#ifdef __SWITCH__
    {
        char line[160];
        snprintf(line, sizeof(line), "B boot:data sf64.o2r=%d starship.o2r=%d", (int) std::filesystem::exists(main_path),
                 (int) std::filesystem::exists(assets_path));
        Switch2Debug_Log(line);
    }
#endif

    if (std::filesystem::exists(main_path)) {
        archiveFiles.push_back(main_path);
    } else {
        if (ShowYesNoBox("Starship - Asset Extraction", "Please provide a Starfox 64 ROM.\n\nSupported Versions:\nUS 1.0\nUS 1.1\n\nAssets will be extracted into an O2R file.") == IDYES) {
            if(!GenAssetFile()){
#ifdef __SWITCH__
                Ship::Switch::ShowSetupErrorAndExit("Starship could not make its game data",
                                                    "Extracting your ROM failed, so sf64.o2r was not created.\n"
                                                    "See GAME-DATA.md in the download.");
#endif
                ShowMessage("Error", "An error occured, no O2R file was generated.\n\nExiting...");
                exit(1);
            } else {
                archiveFiles.push_back(main_path);
            }

            if (ShowYesNoBox("Extraction Complete", "ROM Extracted. Extract another?\n\n Starship supports JP and EU ROMs for voice replacement.\n Voice replacement ROM assets can also be installed in:\n Settings->Language->Install JP/EU Audio") == IDYES) {
#ifdef __SWITCH__
                // Without a JP or EU ROM, carry on with the US game.
                if(!GenAssetFile(false)){
#else
                if(!GenAssetFile()){
#endif
                    ShowMessage("Error", "An error occured, no O2R file was generated.");
                }
            }
        } else {
            exit(1);
        }
    }

    if (std::filesystem::exists(assets_path)) {
        archiveFiles.push_back(assets_path);
    }

#ifdef __SWITCH__
    CheckVoiceArchives();
#endif

    if (const std::string patches_path = Ship::Context::GetPathRelativeToAppDirectory("mods");
        !patches_path.empty()) {
        if (!std::filesystem::exists(patches_path)) {
            std::filesystem::create_directories(patches_path);
        }

        if (std::filesystem::is_directory(patches_path)) {
            for (const auto& p : std::filesystem::recursive_directory_iterator(patches_path)) {
                const auto ext = p.path().extension().string();
                if (StringHelper::IEquals(ext, ".otr") || StringHelper::IEquals(ext, ".o2r")) {
                    archiveFiles.push_back(p.path().generic_string());
                }

                if (StringHelper::IEquals(ext, ".zip")) {
                    SPDLOG_WARN("Zip files should be only used for development purposes, not for distribution");
                    archiveFiles.push_back(p.path().generic_string());
                }
            }
        }
    }

    Switch2Debug_Log("B boot:config");
    this->context->InitConfiguration();    // without this line InitConsoleVariables fails at Config::Reload()
    this->context->InitConsoleVariables(); // without this line the controldeck constructor failes in
                                           // ShipDeviceIndexMappingManager::UpdateControllerNamesFromConfig()
#ifdef __SWITCH__
    SelectVoiceArchive(archiveFiles);
#endif

    auto defaultMappings = std::make_shared<Ship::ControllerDefaultMappings>(
        // KeyboardKeyToButtonMappings - use built-in LUS defaults
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>>(),
        // KeyboardKeyToAxisDirectionMappings - use built-in LUS defaults
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, Ship::KbScancode>>>(),
        // SDLButtonToButtonMappings
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GameControllerButton>>{
            { BTN_A, { SDL_CONTROLLER_BUTTON_A } },
            { BTN_B, { SDL_CONTROLLER_BUTTON_X } },
            { BTN_START, { SDL_CONTROLLER_BUTTON_START } },
#ifdef __SWITCH__
            // SDL buttons are positional: SDL Y is the Switch X button, SDL B
            // is the Switch A button. X looks first person (C-up), A answers
            // calls (C-right). Boost and brake stay on ZL and ZR.
            { BTN_CUP, { SDL_CONTROLLER_BUTTON_Y } },
            { BTN_CRIGHT, { SDL_CONTROLLER_BUTTON_B } },
#else
            { BTN_CLEFT, { SDL_CONTROLLER_BUTTON_Y } },
            { BTN_CDOWN, { SDL_CONTROLLER_BUTTON_B } },
#endif
            { BTN_DUP, { SDL_CONTROLLER_BUTTON_DPAD_UP } },
            { BTN_DDOWN, { SDL_CONTROLLER_BUTTON_DPAD_DOWN } },
            { BTN_DLEFT, { SDL_CONTROLLER_BUTTON_DPAD_LEFT } },
            { BTN_DRIGHT, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT } },
            { BTN_R, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER } },
            { BTN_Z, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER } }
        },
        // SDLButtonToAxisDirectionMappings - use built-in LUS defaults
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GameControllerButton>>>(),
        // SDLAxisDirectionToButtonMappings
        std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GameControllerAxis, int32_t>>>{
            { BTN_CLEFT, { { SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1 } } },
            { BTN_CDOWN, { { SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1 } } },
            { BTN_CUP, { { SDL_CONTROLLER_AXIS_RIGHTY, -1 } } },
            { BTN_CRIGHT, { { SDL_CONTROLLER_AXIS_RIGHTX, 1 } } }
        },
        // SDLAxisDirectionToAxisDirectionMappings - use built-in LUS defaults
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, std::pair<SDL_GameControllerAxis, int32_t>>>>()
    );
    auto controlDeck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>(), defaultMappings);

    Switch2Debug_Log("B boot:archives");
    this->context->InitResourceManager(archiveFiles, {}, 3); // without this line InitWindow fails in Gui::Init()
    this->context->InitConsole(); // without this line the GuiWindow constructor fails in ConsoleWindow::InitElement()

    Switch2Debug_Log("B boot:window");
    auto window = std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({}));

    auto audioChannelsSetting = Ship::Context::GetInstance()->GetConfig()->GetCurrentAudioChannelsSetting();
#ifdef __SWITCH__
    // Audio is made once per game frame. Keep more of it queued (150 ms, not
    // 52 ms) so a slow frame does not empty the buffer and crackle.
    this->context->Init(archiveFiles, {}, 3, { 32000, 1024, 4800, audioChannelsSetting }, window, controlDeck);
    Switch2Debug_BootMark("context");
    Switch2Debug_Log("B boot:context-ready");
    SwitchLoading_Stage("Loading game data", 0.0f, 0.60f);
#else
    this->context->Init(archiveFiles, {}, 3, { 32000, 1024, 1680, audioChannelsSetting }, window, controlDeck);
#endif

#ifndef __SWITCH__
    Ship::Context::GetInstance()->GetLogger()->set_level(
        (spdlog::level::level_enum) CVarGetInteger("gDeveloperTools.LogLevel", 1));
    Ship::Context::GetInstance()->GetLogger()->set_pattern("[%H:%M:%S.%e] [%s:%#] [%l] %v");
#endif

    auto loader = context->GetResourceManager()->GetResourceLoader();
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryAnimV0>(), RESOURCE_FORMAT_BINARY,
                                    "Animation", static_cast<uint32_t>(SF64::ResourceType::AnimData), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinarySkeletonV0>(), RESOURCE_FORMAT_BINARY,
                                    "Skeleton", static_cast<uint32_t>(SF64::ResourceType::Skeleton), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryLimbV0>(), RESOURCE_FORMAT_BINARY,
                                    "Limb", static_cast<uint32_t>(SF64::ResourceType::Limb), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryMessageV0>(), RESOURCE_FORMAT_BINARY,
                                    "Message", static_cast<uint32_t>(SF64::ResourceType::Message), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryMessageLookupV0>(),
                                    RESOURCE_FORMAT_BINARY, "MessageTable",
                                    static_cast<uint32_t>(SF64::ResourceType::MessageTable), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryEnvSettingsV0>(),
                                    RESOURCE_FORMAT_BINARY, "EnvSettings",
                                    static_cast<uint32_t>(SF64::ResourceType::Environment), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryObjectInitV0>(), RESOURCE_FORMAT_BINARY,
                                    "ObjectInit", static_cast<uint32_t>(SF64::ResourceType::ObjectInit), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryHitboxV0>(), RESOURCE_FORMAT_BINARY,
                                    "Hitbox", static_cast<uint32_t>(SF64::ResourceType::Hitbox), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryScriptV0>(), RESOURCE_FORMAT_BINARY,
                                    "Script", static_cast<uint32_t>(SF64::ResourceType::Script), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryScriptCMDV0>(), RESOURCE_FORMAT_BINARY,
                                    "ScriptCMD", static_cast<uint32_t>(SF64::ResourceType::ScriptCmd), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryColPolyV0>(), RESOURCE_FORMAT_BINARY,
                                    "ColPoly", static_cast<uint32_t>(SF64::ResourceType::ColPoly), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryVec3fV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vec3f", static_cast<uint32_t>(SF64::ResourceType::Vec3f), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryVec3sV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vec3s", static_cast<uint32_t>(SF64::ResourceType::Vec3s), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryGenericArrayV0>(),
                                    RESOURCE_FORMAT_BINARY, "GenericArray",
                                    static_cast<uint32_t>(SF64::ResourceType::GenericArray), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 1);

    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vertex", static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLVertexV0>(), RESOURCE_FORMAT_XML, "Vertex",
                                    static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);

    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLDisplayListV0>(), RESOURCE_FORMAT_XML,
                                    "DisplayList", static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);

    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY,
                                    "Matrix", static_cast<uint32_t>(Fast::ResourceType::Matrix), 0);

    loader->RegisterResourceFactory(std::make_shared<Ship::ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY,
                                    "Blob", static_cast<uint32_t>(Ship::ResourceType::Blob), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryAudioTableV0>(), RESOURCE_FORMAT_BINARY,
                                    "AudioTable", static_cast<uint32_t>(SF64::ResourceType::AudioTable), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryAdpcmBookV0>(), RESOURCE_FORMAT_BINARY,
                                    "AdpcmBook", static_cast<uint32_t>(SF64::ResourceType::AdpcmBook), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryDrumV0>(), RESOURCE_FORMAT_BINARY,
                                    "Drum", static_cast<uint32_t>(SF64::ResourceType::Drum), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryEnvelopeV0>(), RESOURCE_FORMAT_BINARY,
                                    "Envelope", static_cast<uint32_t>(SF64::ResourceType::Envelope), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryInstrumentV0>(), RESOURCE_FORMAT_BINARY,
                                    "Instrument", static_cast<uint32_t>(SF64::ResourceType::Instrument), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryAdpcmLoopV0>(), RESOURCE_FORMAT_BINARY,
                                    "AdpcmLoop", static_cast<uint32_t>(SF64::ResourceType::AdpcmLoop), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinarySampleV1>(), RESOURCE_FORMAT_BINARY,
                                    "Sample", static_cast<uint32_t>(SF64::ResourceType::Sample), 1);
    
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryXMLSampleV0>(), RESOURCE_FORMAT_XML,
                                    "Sample", static_cast<uint32_t>(SF64::ResourceType::Sample), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinarySoundFontV0>(), RESOURCE_FORMAT_BINARY,
                                    "SoundFont", static_cast<uint32_t>(SF64::ResourceType::SoundFont), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryXMLSoundFontV0>(), RESOURCE_FORMAT_XML,
                                    "SoundFont", static_cast<uint32_t>(SF64::ResourceType::SoundFont), 0);

    prevAltAssets = CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0);
    gEnableGammaBoost = CVarGetInteger("gGraphics.GammaMode", 0) == 0;
    context->GetResourceManager()->SetAltAssetsEnabled(prevAltAssets);
}

bool GameEngine::GenAssetFile(bool exitOnFail) {
    auto extractor = new GameExtractor();

    if (!extractor->SelectGameFromUI()) {
#ifdef __SWITCH__
        if (!fs::exists(Ship::Context::GetPathRelativeToAppDirectory("sf64.o2r"))) {
            ShowMessage("Error", "No supported US ROM found.\n\nCopy your Star Fox 64 (US) ROM (.z64, .n64 or .v64)\n"
                                 "to /switch/starship/ and start Starship again.\n\nExiting...");
            if (exitOnFail) {
                // Nothing is on screen yet, so say why instead of just closing.
                Ship::Switch::ShowSetupErrorAndExit("Starship is not set up yet",
                                                    "No supported US Star Fox 64 ROM was found in /switch/starship/.\n"
                                                    "See GAME-DATA.md in the download.");
            }
        } else {
            ShowMessage("Error", "No JP or EU ROM found.\n\nCopy your Star Fox 64 JP or EU ROM (.z64, .n64 or .v64)\n"
                                 "to /switch/starship/, then choose Install JP/EU Audio again.");
        }
#else
        ShowMessage("Error", "No ROM selected.\n\nExiting...");
#endif
        if (exitOnFail) {
            exit(1);
        } else {
            return false;
        }
    }

    Switch2Debug_Log("B boot:rom-selected");
    auto game = extractor->ValidateChecksum();
    if (!game.has_value()) {
        ShowMessage("Unsupported ROM", "The provided ROM is not supported.\n\nCheck the readme for a list of supported versions.");
        if (exitOnFail) {
            exit(1);
        } else {
            return false;
        }
    }

    ShowMessage(("Starship - Extraction - Found " + game.value()).c_str(), "The extraction process will now begin.\n\nThis may take a few minutes.", SDL_MESSAGEBOX_INFORMATION);

    Switch2Debug_Log("B boot:extracting");
    const bool extracted = extractor->GenerateOTR();
    Switch2Debug_Log(extracted ? "B boot:extracted" : "B boot:extract-failed");
    return extracted;
}

void GameEngine::Create() {
    const auto instance = Instance = new GameEngine();
    Switch2Debug_Log("B boot:audio-thread");
    instance->AudioInit();
    Switch2Debug_Log("B boot:patches");
    DisplayListPatch::Run();
    Switch2Debug_Log("B boot:gui");
    GameUI::SetupGuiElements();
#if defined(__SWITCH__) || defined(__WIIU__)
    CVarRegisterInteger("gControlNav", 1); // always enable controller nav on switch/wii u
    osSetTime(0);
#endif
    Switch2Debug_Log("B boot:enhancements");
    PortEnhancements_Init();
    Switch2Debug_Log("B boot:engine-ready");
}

void GameEngine::Destroy() {
    Switch2Debug_Log("B exit:start");
    Switch2Debug_Phase(1, "exit:enhancements");
    PortEnhancements_Exit();
    Switch2Debug_Phase(1, "exit:audio-thread");
    AudioExit();
#ifdef __SWITCH__
    // Release the GUI and the context (window, GL, SDL, audio) now, after the
    // audio thread has stopped and before the game's memory is freed: resources
    // released here can still point into it.
    Switch2Debug_Phase(1, "exit:gui");
    GameUI::Destroy();
    Switch2Debug_Log("B exit:context");
    Switch2Debug_Phase(1, "exit:context");
    Instance->context = nullptr;
    Switch2Debug_Phase(1, "exit:memory");
#endif
    for (auto ptr : MemoryPool) {
        free(ptr);
    }
    MemoryPool.clear();
    Switch2Debug_Phase(1, "exit:done");
}

static int sVoiceInstallFrames = -1;
static int sVoiceInstallResult = 0;

void GameEngine::RequestVoiceInstall() {
    if (sVoiceInstallFrames >= 0) {
        return;
    }
    bool found = false;
    for (int language = 1; language <= 2 && !found; language++) {
        if (!IsVoiceInstalled(language)) {
            GameExtractor::SetVoiceTarget(language);
            GameExtractor extractor;
            found = extractor.SelectGameFromUI();
        }
    }
    GameExtractor::SetVoiceTarget(0);
    if (!found) {
        sVoiceInstallResult = 5;
        return;
    }
    // Let the install message reach the screen before extraction blocks the game.
    sVoiceInstallFrames = 20;
}

// Bit 0 is JP and bit 1 is EU. Checked once, then again after each install.
static int sVoicesOnDisk = -1;

static int VoicesOnDisk() {
    if (sVoicesOnDisk < 0) {
        std::error_code error;
        const std::filesystem::path mods = Ship::Context::GetPathRelativeToAppDirectory("mods");
        sVoicesOnDisk = (std::filesystem::exists(mods / "sf64jp.o2r", error) ? 1 : 0) |
                        (std::filesystem::exists(mods / "sf64eu.o2r", error) ? 2 : 0);
    }
    return sVoicesOnDisk;
}

const char* GameEngine::VoiceInstallLabel() {
    static const char* const labels[] = {
        "Install JP/EU Audio###InstallVoices",
        "Install JP/EU Audio (JP Installed)###InstallVoices",
        "Install JP/EU Audio (EU Installed)###InstallVoices",
        "Install JP/EU Audio (Installed)###InstallVoices",
    };
    return labels[VoicesOnDisk()];
}

bool GameEngine::IsVoiceInstalled(int language) {
    return (VoicesOnDisk() & language) != 0;
}

int GameEngine::ActiveVoicePack() {
    return sActiveVoicePack;
}

bool GameEngine::IsVoiceInstallPending() {
    return sVoiceInstallFrames >= 0;
}

int GameEngine::TakeVoiceInstallResult() {
    const int result = sVoiceInstallResult;
    sVoiceInstallResult = 0;
    return result;
}

void GameEngine::ProcessVoiceInstall() {
    if (sVoiceInstallFrames < 0 || sVoiceInstallFrames-- > 0) {
        return;
    }
    sVoiceInstallFrames = -1;
    // Install every language that has a ROM in the folder.
    int result = 5;
    for (int language = 1; language <= 2; language++) {
        if (IsVoiceInstalled(language)) {
            continue;
        }
        GameExtractor::SetVoiceTarget(language);
        GameExtractor extractor;
        if (!extractor.SelectGameFromUI()) {
            continue;
        }
        if (!GenAssetFile(false)) {
            result = GameExtractor::LastFailureWasStorage() ? 2 : 3;
            break;
        }
        result = 1;
    }
    GameExtractor::SetVoiceTarget(0);
    sVoicesOnDisk = -1;
    sVoiceInstallResult = result;
}

// Builds known shaders behind the loading screen, then hands over to the game.
void GameEngine::PrepareShaders() {
#ifdef __SWITCH__
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    auto interpreter = wnd != nullptr ? wnd->GetInterpreterWeak().lock() : nullptr;
    if (interpreter != nullptr) {
        SwitchShaderCache_Open();
        std::vector<std::pair<uint64_t, uint64_t>> shaders;
        for (const auto& entry : SwitchShaderCache_KnownShaders()) {
            shaders.push_back(entry);
        }
        SwitchLoading_Stage("Preparing shaders", 0.60f, 0.72f);
        interpreter->PrepareShaders(shaders, SwitchLoading_Update);
        SwitchShaderCache_ReleaseBinaries();
        Switch2Debug_BootMark("shaders");
        SwitchLoading_Stage("Preparing graphics", 0.72f, 1.0f);
        interpreter->WarmShaders(shaders, SwitchLoading_Update);
        Switch2Debug_BootMark("graphics");
    }
    SwitchLoading_Finish();
#ifdef __SWITCH__
    {
        // The voice pack got the game to its first frame.
        std::error_code guardError;
        std::filesystem::remove(VoiceGuardPath(), guardError);
    }
#endif
#endif
}

void GameEngine::StartFrame() const {
    using Ship::KbScancode;
    const int32_t dwScancode = this->context->GetWindow()->GetLastScancode();
    this->context->GetWindow()->SetLastScancode(-1);

    switch (dwScancode) {
        case KbScancode::LUS_KB_TAB: {
            // Toggle HD Assets
            CVarSetInteger("gEnhancements.Mods.AlternateAssets", !CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0));
            break;
        }
        case KbScancode::LUS_KB_F4: {
            gNextGameState = GSTATE_BOOT;
            break;
        }
        default:
            break;
    }
}

#if 0
// Values for 44100 hz
#define SAMPLES_HIGH 752
#define SAMPLES_LOW 720
#else
// Values for 32000 hz
#define SAMPLES_HIGH 560
#define SAMPLES_LOW 528

#endif

#define MAX_NUM_AUDIO_CHANNELS 6

extern "C" u16 audBuffer = 0;
#include <sf64audio_provisional.h>

extern "C" volatile s32 gAudioTaskCountQ;
int frames = 0;
extern "C" int countermin = 0;

extern "C" unsigned short samples_high = SAMPLES_HIGH;
extern "C" unsigned short samples_low = SAMPLES_LOW;

void GameEngine::HandleAudioThread() {
#ifdef PIPE_DEBUG
    std::ofstream outfile("audio.bin", std::ios::binary | std::ios::app);
#endif
    while (audio.running) {
        {
            std::unique_lock<std::mutex> Lock(audio.mutex);
            while (!audio.processing && audio.running) {
                audio.cv_to_thread.wait(Lock);
            }
            if (!audio.running) {
                break;
            }
        }

        // gVIsPerFrame = 2;

#define AUDIO_FRAMES_PER_UPDATE (gVIsPerFrame > 0 ? gVIsPerFrame : 1)
#define MAX_AUDIO_FRAMES_PER_UPDATE 5 // Compile-time constant with max value of gVIsPerFrame

        std::unique_lock<std::mutex> Lock(audio.mutex);
        int samples_left = AudioPlayerBuffered();
        u32 num_audio_samples = samples_left < AudioPlayerGetDesiredBuffered() ? (((samples_high))) : (((samples_low)));

        frames++;

        if (frames > 60) {
            countermin++;
        }

        const int32_t num_audio_channels = GetNumAudioChannels();

#ifdef __SWITCH__
        // Audio is made once per drawn frame, so a late frame leaves too little
        // of it: the music slows down and the queue runs dry. Make extra updates
        // until the queue is back at its target, except while the audio heap resets.
        constexpr int kMaxAudioUpdates = 9;
        int audioUpdates = AUDIO_FRAMES_PER_UPDATE;
        while (gAudioResetStep == 0 && audioUpdates < kMaxAudioUpdates &&
               samples_left + audioUpdates * (int) num_audio_samples < AudioPlayerGetDesiredBuffered()) {
            audioUpdates++;
        }
        s16 audio_buffer[SAMPLES_HIGH * MAX_NUM_AUDIO_CHANNELS * kMaxAudioUpdates] = { 0 };
#else
        const int audioUpdates = AUDIO_FRAMES_PER_UPDATE;
        s16 audio_buffer[SAMPLES_HIGH * MAX_NUM_AUDIO_CHANNELS * MAX_AUDIO_FRAMES_PER_UPDATE] = { 0 };
#endif
        for (int i = 0; i < audioUpdates; i++) {
            AudioThread_CreateNextAudioBuffer(audio_buffer + i * (num_audio_samples * num_audio_channels),
                                              num_audio_samples);
        }
#ifdef PIPE_DEBUG
        if (outfile.is_open()) {
            outfile.write(reinterpret_cast<char*>(audio_buffer),
                          num_audio_samples * (sizeof(int16_t) * num_audio_channels * AUDIO_FRAMES_PER_UPDATE));
        }
#endif
        AudioPlayerPlayFrame((u8*) audio_buffer,
                             num_audio_samples * (sizeof(int16_t) * num_audio_channels * audioUpdates));
        
        audio.processing = false;
        audio.cv_from_thread.notify_one();
    }
#ifdef PIPE_DEBUG
    outfile.close();
#endif
}

void GameEngine::StartAudioFrame() {
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        audio.processing = true;
    }
    audio.cv_to_thread.notify_one();
}

void GameEngine::EndAudioFrame() {
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        while (audio.processing) {
            audio.cv_from_thread.wait(Lock);
        }
    }
}

void GameEngine::AudioInit() {
    if (!audio.running) {
        audio.running = true;
        audio.thread = std::thread(HandleAudioThread);
    }
}

void GameEngine::AudioExit() {
    {
        std::unique_lock lock(audio.mutex);
        audio.running = false;
    }
    audio.cv_to_thread.notify_all();
    // Wait until the audio thread quit
    audio.thread.join();
}

void GameEngine::RunCommands(Gfx* Commands, const std::vector<std::unordered_map<Mtx*, MtxF>>& mtx_replacements) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());

    if (wnd == nullptr) {
        return;
    }

    auto interpreter = wnd->GetInterpreterWeak().lock().get();

    // Process window events for resize, mouse, keyboard events
    wnd->HandleEvents();

    interpreter->mInterpolationIndex = 0;

    for (const auto& m : mtx_replacements) {
        wnd->DrawAndRunGraphicsCommands(Commands, m);
        interpreter->mInterpolationIndex++;
    }

    bool curAltAssets = CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0);
    if (prevAltAssets != curAltAssets) {
        prevAltAssets = curAltAssets;
        Ship::Context::GetInstance()->GetResourceManager()->SetAltAssetsEnabled(curAltAssets);
        gfx_texture_cache_clear();
    }
}

void GameEngine::ProcessGfxCommands(Gfx* commands) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());

    if (wnd == nullptr) {
        return;
    }

    if(gEnableGammaBoost) {
        wnd->EnableSRGBMode();
    }
    wnd->SetRendererUCode(UcodeHandlers::ucode_f3dex);

    std::vector<std::unordered_map<Mtx*, MtxF>> mtx_replacements;
    int target_fps = GameEngine::Instance->GetInterpolationFPS();
    static int last_fps;
    static int last_update_rate;
    static int time;
    int fps = target_fps;
    int original_fps = 60 / gVIsPerFrame;

    if (target_fps == 20 || original_fps > target_fps) {
        fps = original_fps;
    }

    if (last_fps != fps || last_update_rate != gVIsPerFrame) {
        time = 0;
    }

    // time_base = fps * original_fps (one second)
    int next_original_frame = fps;

    while (time + original_fps <= next_original_frame) {
        time += original_fps;
        if (time != next_original_frame) {
            mtx_replacements.push_back(FrameInterpolation_Interpolate((float) time / next_original_frame));
        } else {
            mtx_replacements.emplace_back();
        }
    }

    time -= fps;

    if (wnd != nullptr) {
        wnd->SetTargetFps(fps);
        wnd->SetMaximumFrameLatency(CVarGetInteger("gRenderParallelization", 1) ? 2 : 1);
    }

    // When the gfx debugger is active, only run with the final mtx
    if (GfxDebuggerIsDebugging()) {
        mtx_replacements.clear();
        mtx_replacements.emplace_back();
    }

    RunCommands(commands, mtx_replacements);

    last_fps = fps;
    last_update_rate = gVIsPerFrame;
}

uint32_t GameEngine::GetInterpolationFPS() {
    if (CVarGetInteger("gMatchRefreshRate", 0)) {
        return Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate();

    } else if (CVarGetInteger("gVsyncEnabled", 1) ||
               !Ship::Context::GetInstance()->GetWindow()->CanDisableVerticalSync()) {
        return std::min<uint32_t>(Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate(),
                                  CVarGetInteger("gInterpolationFPS", 60));
    }

    return CVarGetInteger("gInterpolationFPS", 60);
}

uint32_t GameEngine::GetInterpolationFrameCount()
{
	return ceil((float)GetInterpolationFPS() / (60.0f / gVIsPerFrame));
}

extern "C" uint32_t GameEngine_GetInterpolationFrameCount() {
	return GameEngine::GetInterpolationFrameCount();
}

void GameEngine::ShowMessage(const char* title, const char* message, SDL_MessageBoxFlags type) {
#if defined(__SWITCH__)
    SPDLOG_ERROR(message);
    {
        char line[220];
        snprintf(line, sizeof(line), "B message: %s: %s", title, message);
        Switch2Debug_Log(line);
    }
#else
    SDL_ShowSimpleMessageBox(type, title, message, nullptr);
    SPDLOG_ERROR(message);
#endif
}

int GameEngine::ShowYesNoBox(const char* title, const char* box) {
    int ret;
#ifdef _WIN32
    ret = MessageBoxA(nullptr, box, title, MB_YESNO | MB_ICONQUESTION);
#elif defined(__SWITCH__)
    SPDLOG_ERROR(box);
    return IDYES;
#else
    SDL_MessageBoxData boxData = { 0 };
    SDL_MessageBoxButtonData buttons[2] = { { 0 } };

    buttons[0].buttonid = IDYES;
    buttons[0].text = "Yes";
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[1].buttonid = IDNO;
    buttons[1].text = "No";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    boxData.numbuttons = 2;
    boxData.flags = SDL_MESSAGEBOX_INFORMATION;
    boxData.message = box;
    boxData.title = title;
    boxData.buttons = buttons;
    SDL_ShowMessageBox(&boxData, &ret);
#endif
    return ret;
}

bool GameEngine::HasVersion(SF64Version ver){
    auto versions = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions();
    return std::find(versions.begin(), versions.end(), ver) != versions.end();
}

extern "C" bool GameEngine_HasVersion(SF64Version ver) {
    return GameEngine::HasVersion(ver);
}

extern "C" uint32_t GameEngine_GetSampleRate() {
    auto player = Ship::Context::GetInstance()->GetAudio()->GetAudioPlayer();
    if (player == nullptr) {
        return 0;
    }

    if (!player->IsInitialized()) {
        return 0;
    }

    return player->GetSampleRate();
}

extern "C" uint32_t GameEngine_GetSamplesPerFrame() {
    return SAMPLES_PER_FRAME;
}

// End

Fast::Interpreter* GameEngine_GetInterpreter() {
    return static_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow())
             ->GetInterpreterWeak()
             .lock()
             .get();
}

extern "C" float GameEngine_GetAspectRatio() {
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.aspect_ratio;
}

extern "C" uint32_t GameEngine_GetGameVersion() {
    return 0x00000001;
}

static const char* sOtrSignature = "__OTR__";

extern "C" uint8_t GameEngine_OTRSigCheck(const char* data) {
    if (data == nullptr) {
        return 0;
    }
    return strncmp(data, sOtrSignature, strlen(sOtrSignature)) == 0;
}

extern "C" void GameEngine_GetTextureInfo(const char* path, int32_t* width, int32_t* height, float* scale, bool* custom) {
    if(GameEngine_OTRSigCheck(path) != 1){
        *custom = false;
        return;
    }
    std::shared_ptr<Fast::Texture> tex = std::static_pointer_cast<Fast::Texture>(
        Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(path));
    *width = tex->Width;
    *height = tex->Height;
    *scale = tex->VPixelScale;
    *custom = tex->Flags & (1 << 0);
}

extern "C" float __cosf(float angle) throw() {
    return cosf(angle);
}

extern "C" float __sinf(float angle) throw() {
    return sinf(angle);
}

extern "C" float SIN_DEG(float angle) {
    return __sinf(M_DTOR * angle);
}
extern "C" float COS_DEG(float angle) {
    return __cosf(M_DTOR * angle);
}

struct TimedEntry {
    uint64_t duration;
    TimerAction action;
    int32_t* address;
    int32_t value;
    bool active;
};

std::vector<TimedEntry> gTimerTasks;

uint64_t Timer_GetCurrentMillis() {
    return SDL_GetTicks();
}

extern "C" s32 Timer_CreateTask(u64 time, TimerAction action, s32* address, s32 value) {
    const auto millis = Timer_GetCurrentMillis();
    TimedEntry entry = {
        .duration = millis + CYCLES_TO_MSEC_PC(time),
        .action = action,
        .address = address,
        .value = value,
        .active = true,
    };

    gTimerTasks.push_back(entry);

    return gTimerTasks.size() - 1;
}

extern "C" void Timer_Increment(int32_t* address, int32_t value) {
    *address += value;
}

extern "C" void Timer_SetValue(int32_t* address, int32_t value) {
    *address = value;
}

void Timer_CompleteTask(TimedEntry& task) {
    if (task.action != nullptr) {
        task.action(task.address, task.value);
    }
    task.active = false;
}

extern "C" void Timer_Update() {

    if (gTimerTasks.empty()) {
        return;
    }

    const auto millis = Timer_GetCurrentMillis();

    for (auto& task : gTimerTasks) {
        if (task.active && millis >= task.duration) {
            Timer_CompleteTask(task);
        }
    }
}

// Gets the width of the main ImGui window
extern "C" uint32_t OTRGetCurrentWidth() {
    return GameEngine::Instance->context->GetWindow()->GetWidth();
}

// Gets the height of the main ImGui window
extern "C" uint32_t OTRGetCurrentHeight() {
    return GameEngine::Instance->context->GetWindow()->GetHeight();
}

extern "C" float OTRGetHUDAspectRatio() {
    if (CVarGetInteger("gHUDAspectRatio.Enabled", 0) == 0 || CVarGetInteger("gHUDAspectRatio.X", 0) == 0 || CVarGetInteger("gHUDAspectRatio.Y", 0) == 0) {
        return GameEngine_GetAspectRatio();
    }
    return ((float)CVarGetInteger("gHUDAspectRatio.X", 1) / (float)CVarGetInteger("gHUDAspectRatio.Y", 1));
}

extern "C" float OTRGetDimensionFromLeftEdge(float v) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 - interpreter->mNativeDimensions.height / 2 * interpreter->mCurDimensions.aspect_ratio + (v));
}

extern "C" float OTRGetDimensionFromRightEdge(float v) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 + interpreter->mNativeDimensions.height / 2 * interpreter->mCurDimensions.aspect_ratio -
            (interpreter->mNativeDimensions.width - v));
}

extern "C" float OTRGetDimensionFromLeftEdgeForcedAspect(float v, float aspectRatio) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 - interpreter->mNativeDimensions.height / 2 * (aspectRatio > 0 ? aspectRatio : interpreter->mCurDimensions.aspect_ratio) + (v));
}

extern "C" float OTRGetDimensionFromRightEdgeForcedAspect(float v, float aspectRatio) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 + interpreter->mNativeDimensions.height / 2 * (aspectRatio > 0 ? aspectRatio : interpreter->mCurDimensions.aspect_ratio) -
            (interpreter->mNativeDimensions.width - v));
}

extern "C" float OTRGetDimensionFromLeftEdgeOverride(float v) {
    return OTRGetDimensionFromLeftEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" float OTRGetDimensionFromRightEdgeOverride(float v) {
    return OTRGetDimensionFromRightEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

// Gets the width of the current render target area
extern "C" uint32_t OTRGetGameRenderWidth() {
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.width;
}

// Gets the height of the current render target area
extern "C" uint32_t OTRGetGameRenderHeight() {
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.height;
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdge(float v) {
    return ((int) floorf(OTRGetDimensionFromLeftEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdge(float v) {
    return ((int) ceilf(OTRGetDimensionFromRightEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdgeForcedAspect(float v, float aspectRatio) {
    return ((int) floorf(OTRGetDimensionFromLeftEdgeForcedAspect(v, aspectRatio)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdgeForcedAspect(float v, float aspectRatio) {
    return ((int) ceilf(OTRGetDimensionFromRightEdgeForcedAspect(v, aspectRatio)));
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdgeOverride(float v) {
    return OTRGetRectDimensionFromLeftEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" int16_t OTRGetRectDimensionFromRightEdgeOverride(float v) {
    return OTRGetRectDimensionFromRightEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" int32_t OTRConvertHUDXToScreenX(int32_t v) {
    auto interpreter = GameEngine_GetInterpreter();
    float gameAspectRatio = interpreter->mCurDimensions.aspect_ratio;
    int32_t gameHeight = interpreter->mCurDimensions.height;
    int32_t gameWidth = interpreter->mCurDimensions.width;
    float hudAspectRatio = 4.0f / 3.0f;
    int32_t hudHeight = gameHeight;
    int32_t hudWidth = hudHeight * hudAspectRatio;
    float hudScreenRatio = (hudWidth / 320.0f);
    float hudCoord = v * hudScreenRatio;
    float gameOffset = (gameWidth - hudWidth) / 2;
    float gameCoord = hudCoord + gameOffset;
    float gameScreenRatio = (320.0f / gameWidth);
    float screenScaledCoord = gameCoord * gameScreenRatio;
    int32_t screenScaledCoordInt = screenScaledCoord;
    return screenScaledCoordInt;
}

extern "C" void* GameEngine_Malloc(size_t size) {
    MemoryPool.push_back(new uint8_t[size]);
    return (void*) MemoryPool.back();
}

extern "C" void GameEngine_Free(void* ptr) {
    for (auto it = MemoryPool.begin(); it != MemoryPool.end(); ++it) {
        if (*it == ptr) {
            free(ptr);
            MemoryPool.erase(it);
            break;
        }
    }
}
