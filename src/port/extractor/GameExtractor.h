#pragma once

#include "Companion.h"
#include <filesystem>
#include <vector>
#include <cstdint>

class GameExtractor {
public:
    static bool GenAssetFile();
    std::optional<std::string> ValidateChecksum() const;
    bool SelectGameFromUI();
    bool GenerateOTR() const;
#ifdef __SWITCH__
    // True if the file ends with a whole zip directory, so it was fully written.
    static bool IsCompleteArchive(const std::filesystem::path& path);
    // The last GenerateOTR failed because the SD card is full or could not be written.
    static bool LastFailureWasStorage();
    // The extractor keeps global state that is unsafe to destroy at exit.
    static bool HasRun();
    // Limits the voice ROM search: 0 any, 1 JP, 2 EU.
    static void SetVoiceTarget(int language);
#endif
private:
    fs::path mGamePath;
    std::vector<uint8_t> mGameData;
};