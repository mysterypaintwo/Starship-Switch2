#include "GameExtractor.h"

#include <fstream>

#include "Context.h"
#include "spdlog/spdlog.h"
#ifdef __SWITCH__
#include "spdlog/sinks/null_sink.h"
#include <sys/statvfs.h>
#include <cstdio>
#endif
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

// More than any archive the extractor writes (the EU voices are about 9 MB).
constexpr uint64_t kInstallSpaceNeeded = 64ULL * 1024 * 1024;
bool sLastFailureWasStorage = false;
bool sExtractorRan = false;
int sVoiceTarget = 0;

bool HasInstallSpace(const std::string& directory) {
    struct statvfs info;
    if (statvfs(directory.c_str(), &info) != 0) {
        return true; // Unknown: the write is still checked afterward.
    }
    return static_cast<uint64_t>(info.f_bavail) * info.f_frsize >= kInstallSpaceNeeded;
}

void RemoveIfPresent(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
}
} // namespace

bool GameExtractor::IsCompleteArchive(const std::filesystem::path& path) {
    FILE* file = fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t end[22];
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    const bool read = size >= 22 && fseek(file, size - 22, SEEK_SET) == 0 && fread(end, 1, 22, file) == 22;
    fclose(file);
    if (!read || end[0] != 'P' || end[1] != 'K' || end[2] != 5 || end[3] != 6) {
        return false;
    }
    const uint32_t directorySize = end[12] | (end[13] << 8) | (end[14] << 16) | ((uint32_t) end[15] << 24);
    const uint32_t directoryStart = end[16] | (end[17] << 8) | (end[18] << 16) | ((uint32_t) end[19] << 24);
    return static_cast<uint64_t>(directoryStart) + directorySize + 22 == static_cast<uint64_t>(size);
}

bool GameExtractor::LastFailureWasStorage() {
    return sLastFailureWasStorage;
}

void GameExtractor::SetVoiceTarget(int language) {
    sVoiceTarget = language;
}

bool GameExtractor::HasRun() {
    return sExtractorRan;
}
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
        const bool isTarget = sVoiceTarget == 0 || (sVoiceTarget == 1) == isJp;
        if (wantVoices ? (IsVoiceRom(name) && isTarget && !(isJp ? haveJp : haveEu)) : IsUsRom(name)) {
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

    // JP and EU voices are written to mods/, which may not exist yet on first boot.
    std::error_code modsError;
    fs::create_directories(fs::path(game_path) / "mods", modsError);

#ifdef __SWITCH__
    sLastFailureWasStorage = false;
    const fs::path hashFile = fs::path(game_path) / "torch.hash.yml";
    if (!HasInstallSpace(game_path)) {
        sLastFailureWasStorage = true;
        SPDLOG_ERROR("Not enough free space on the SD card to extract");
        return false;
    }
    // A leftover from an interrupted run must not influence this one.
    RemoveIfPresent(hashFile);
    sExtractorRan = true;
#endif

    // Torch clears Companion::Instance when it finishes, so keep our own pointer.
    auto* companion = new Companion(this->mGameData, ArchiveType::O2R, false, assets_path, game_path);
    Companion::Instance = companion;

#ifdef __SWITCH__
    // Torch logs about 20,000 lines per ROM and the game's logger flushes each
    // one to the SD card, which turns seconds of work into minutes.
    const auto gameLogger = spdlog::default_logger();
    const auto gameLogLevel = gameLogger->level();
    spdlog::set_default_logger(
        std::make_shared<spdlog::logger>("extractor", std::make_shared<spdlog::sinks::null_sink_mt>()));
#endif

    bool ok = true;
#ifdef __SWITCH__
    std::string failure;
#endif
    try {
        companion->Init(ExportType::Binary);
    } catch (const std::exception& e) {
        ok = false;
#ifdef __SWITCH__
        failure = e.what();
#endif
    }

#ifdef __SWITCH__
    spdlog::set_default_logger(gameLogger);
    spdlog::set_level(gameLogLevel);
    RemoveIfPresent(hashFile);

    // Several ROMs can be installed in a row, so do not keep this one in memory.
    std::vector<uint8_t>().swap(companion->GetRomData());

    // Torch does not check its final write, so a full SD card leaves a cut-off file.
    const std::string output = companion->GetOutputPath();
    if (ok && (output.empty() || !IsCompleteArchive(output))) {
        ok = false;
        sLastFailureWasStorage = !output.empty();
        failure = "the archive was not fully written";
    }
    if (!ok) {
        SPDLOG_ERROR("Extraction failed: {}", failure);
        if (!output.empty()) {
            RemoveIfPresent(output);
        }
        return false;
    }
#else
    if (!ok) {
        return false;
    }
#endif
    return true;
}
