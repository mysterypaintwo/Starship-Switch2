#include "GameExtractor.h"

#include <fstream>

#include "Context.h"
#include "spdlog/spdlog.h"
#include <port/Engine.h>
#if !defined(__IOS__) && !defined(__ANDROID__) && !defined(__SWITCH__)
#include "portable-file-dialogs.h"
#endif

std::unordered_map<std::string, std::string> mGameList = {
    { "d8b1088520f7c5f81433292a9258c1184afa1457", "Star Fox 64 (U) (V1.0)" },
    { "63b69f0ef36306257481afc250f9bc304c7162b2", "Star Fox 64 (U) (V1.0) (Uncompressed)" },
    { "09f0d105f476b00efa5303a3ebc42e60a7753b7a", "Star Fox 64 (U) (V1.1)" },
    { "f7475fb11e7e6830f82883412638e8390791ab87", "Star Fox 64 (U) (V1.1) (Uncompressed)" },
    { "9bd71afbecf4d0a43146e4e7a893395e19bf3220", "Star Fox 64 (J) (V1.0)" },
    { "d064229a32cc05ab85e2381ce07744eb3ffaf530", "Star Fox 64 (J) (V1.0) (Uncompressed)" },
    { "05b307b8804f992af1a1e2fbafbd588501fdf799", "Star Fox 64 (E) (V1.0)" },
    { "09f5d5c14219fc77a36c5a6ad5e63f7abd8b3385", "Star Fox 64 (E) (V1.0) (Uncompressed)" },
    { "e6dad7523ff8f83fad6fbdb59d472b4f76340c2b", "Star Fox 64 (E) (V1.0) (Spanish Romhack)" },
    { "c8a10699dea52f4bb2e2311935c1376dfb352e7a", "Star Fox 64 (C) (V1.1)" },
    { "3a05aba5549fa71e8b16a0c6e2c8481b070818a9", "Star Fox 64 (C) (V1.1) (Uncompressed)" },
};

#ifdef __SWITCH__
namespace {
// Puts a ROM in .z64 (big-endian) byte order. Returns false if the data is not
// an N64 ROM in any of the three byte orders.
bool ToBigEndianRom(std::vector<uint8_t>& data) {
    if (data.size() < 0x1000) {
        return false;
    }
    const uint32_t magic = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (magic == 0x80371240) { // .z64
        return true;
    }
    if (magic == 0x37804012) { // .v64: 16-bit words swapped
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            std::swap(data[i], data[i + 1]);
        }
        return true;
    }
    if (magic == 0x40123780) { // .n64: 32-bit little endian
        for (size_t i = 0; i + 3 < data.size(); i += 4) {
            std::swap(data[i], data[i + 3]);
            std::swap(data[i + 1], data[i + 2]);
        }
        return true;
    }
    return false;
}

bool IsUsRom(const std::string& name) {
    return name.find("(U)") != std::string::npos;
}

bool IsVoiceRom(const std::string& name) {
    return name.find("(J)") != std::string::npos || name.find("(E)") != std::string::npos;
}
} // namespace
#endif

bool GameExtractor::SelectGameFromUI() {
#if defined(__SWITCH__)
    // The console has no file picker. Look for ROMs (.z64, .n64 or .v64, any
    // name) in the Starship folder. Without sf64.o2r, take a US ROM (the game);
    // with it, take a JP or EU ROM whose voices are not installed yet.
    const fs::path appDir = Ship::Context::GetAppDirectoryPath();
    const bool wantVoices = fs::exists(Ship::Context::GetPathRelativeToAppDirectory("sf64.o2r"));
    const bool haveJp = fs::exists(appDir / "mods" / "sf64jp.o2r");
    const bool haveEu = fs::exists(appDir / "mods" / "sf64eu.o2r");

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(appDir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        for (auto& c : ext) {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
        if (ext != ".z64" && ext != ".n64" && ext != ".v64") {
            continue;
        }
        std::ifstream file(entry.path(), std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
        file.close();
        if (!ToBigEndianRom(data)) {
            SPDLOG_INFO("Not an N64 ROM: {}", entry.path().string());
            continue;
        }
        const auto game = mGameList.find(Companion::CalculateHash(data));
        if (game == mGameList.end()) {
            SPDLOG_INFO("Unsupported ROM: {}", entry.path().string());
            continue;
        }
        const std::string& name = game->second;
        const bool isJp = name.find("(J)") != std::string::npos;
        if (wantVoices ? (IsVoiceRom(name) && !(isJp ? haveJp : haveEu)) : IsUsRom(name)) {
            SPDLOG_INFO("Using {} for {}", entry.path().string(), name);
            this->mGamePath = entry.path();
            this->mGameData = std::move(data);
            return true;
        }
    }
    SPDLOG_INFO("No {} ROM found in {}", wantVoices ? "JP or EU" : "US", appDir.string());
    return false;
#elif !defined(__IOS__) && !defined(__ANDROID__)
    auto selection = pfd::open_file("Select a file", ".", { "N64 Roms", "*.z64" }).result();

    if (selection.empty()) {
        return false;
    }

    this->mGamePath = selection[0];
#else
    this->mGamePath = Ship::Context::GetPathRelativeToAppDirectory("baserom.us.rev1.z64");
#endif

    std::ifstream file(this->mGamePath, std::ios::binary);
    this->mGameData = std::vector<uint8_t>( std::istreambuf_iterator( file ), {} );
    file.close();
    return true;
}

std::optional<std::string> GameExtractor::ValidateChecksum() const {
    // A missing or short file is not a ROM; Cartridge::Initialize reads its header.
    if (this->mGameData.size() < 0x1000) {
        return std::nullopt;
    }
    const auto rom = new N64::Cartridge(this->mGameData);
    rom->Initialize();
    auto hash = rom->GetHash();
    
    if (mGameList.find(hash) == mGameList.end()) {
        return std::nullopt;
    }

    return mGameList[hash];
}

bool GameExtractor::GenerateOTR() const {
    const std::string assets_path = Ship::Context::GetAppBundlePath();
    const std::string game_path = Ship::Context::GetAppDirectoryPath();

    Companion::Instance = new Companion(this->mGameData, ArchiveType::O2R, false, assets_path, game_path);

    try {
        Companion::Instance->Init(ExportType::Binary);
    } catch (const std::exception& e) {
        return false;
    }

    return true;
}
