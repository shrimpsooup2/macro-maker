#pragma once

#include <string>
#include <string_view>

namespace mm {

enum class Effort {
    Quick,
    Balanced,
    Thorough,
    Custom,
};

enum class VerifyMode {
    Fast,       // replay through the real game as quickly as the machine allows
    Watch,      // replay at normal speed so you can see it
    Off,        // write the macro on the simulator's word alone
};

struct Snapshot {
    Effort effort = Effort::Balanced;

    // Resolved search numbers: on a preset they come from the preset, on Custom from the
    // matching setting.
    int beamWidth = 192;
    int flightInterval = 3;
    double searchSeconds = 60.0;

    VerifyMode verify = VerifyMode::Fast;
    int verifyRounds = 4;
    double verifyMs = 10.0;

    bool writeGdr2 = true;
    bool sendToEclipse = true;
    bool writeGdr = false;
    bool writeMhr = true;
    bool writePlain = false;
    std::string extraFolder;
    int frameOffset = 0;
    bool savePartial = true;

    bool autoPlay = false;

    bool flyingLimits = false;
    bool showOverlay = false;
    bool showHud = true;
    double hudY = 0.82;
    double hudScale = 1.0;

    bool debugLog = false;
};

class SettingsCache {
public:
    static SettingsCache& get();

    void refresh();
    Snapshot const& snapshot() const { return m_snapshot; }

private:
    Snapshot m_snapshot;
};

inline Snapshot const& settings() {
    return SettingsCache::get().snapshot();
}

constexpr const char* kMakeKeySetting = "key-make";
constexpr const char* kPlayKeySetting = "key-play";
constexpr const char* kRecordKeySetting = "key-record";
constexpr const char* kOverlayKeySetting = "key-overlay";
constexpr const char* kStopKeySetting = "key-stop";

constexpr const char* kLogTag = "[macro-maker]";

} // namespace mm
