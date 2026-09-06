#include "Settings.hpp"

#include <Geode/loader/Mod.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace mm {

namespace {

bool boolSetting(char const* key, bool fallback) {
    auto* mod = Mod::get();
    if (!mod) return fallback;
    return mod->getSettingValue<bool>(key);
}

int intSetting(char const* key, int fallback) {
    auto* mod = Mod::get();
    if (!mod) return fallback;
    return static_cast<int>(mod->getSettingValue<int64_t>(key));
}

double floatSetting(char const* key, double fallback) {
    auto* mod = Mod::get();
    if (!mod) return fallback;
    double value = mod->getSettingValue<double>(key);
    if (!std::isfinite(value)) return fallback;
    return value;
}

std::string stringSetting(char const* key, char const* fallback) {
    auto* mod = Mod::get();
    if (!mod) return fallback;
    return mod->getSettingValue<std::string>(key);
}

Effort readEffort() {
    std::string choice = stringSetting("effort", "Balanced");
    if (choice == "Quick") return Effort::Quick;
    if (choice == "Thorough") return Effort::Thorough;
    if (choice == "Custom") return Effort::Custom;
    return Effort::Balanced;
}

VerifyMode readVerify() {
    std::string choice = stringSetting("verify", "Fast");
    if (choice == "Watch") return VerifyMode::Watch;
    if (choice == "Off") return VerifyMode::Off;
    return VerifyMode::Fast;
}

} // namespace

SettingsCache& SettingsCache::get() {
    static SettingsCache instance;
    return instance;
}

void SettingsCache::refresh() {
    Snapshot next;

    next.effort = readEffort();
    switch (next.effort) {
        case Effort::Quick:
            next.beamWidth = 64;
            next.flightInterval = 4;
            next.searchSeconds = 20.0;
            break;
        case Effort::Thorough:
            next.beamWidth = 512;
            next.flightInterval = 2;
            next.searchSeconds = 300.0;
            break;
        case Effort::Custom:
            next.beamWidth = intSetting("beam-width", 192);
            next.flightInterval = intSetting("flight-interval", 3);
            next.searchSeconds = floatSetting("search-seconds", 60.0);
            break;
        case Effort::Balanced:
        default:
            next.beamWidth = 192;
            next.flightInterval = 3;
            next.searchSeconds = 60.0;
            break;
    }

    next.beamWidth = std::clamp(next.beamWidth, 8, 3072);
    next.flightInterval = std::clamp(next.flightInterval, 1, 12);
    next.searchSeconds = std::clamp(next.searchSeconds, 5.0, 900.0);

    next.verify = readVerify();
    next.verifyRounds = std::clamp(intSetting("verify-rounds", 4), 0, 20);
    next.verifyMs = std::clamp(floatSetting("verify-ms", 10.0), 2.0, 60.0);

    next.writeGdr = boolSetting("write-gdr", true);
    next.writeMhr = boolSetting("write-mhr", true);
    next.writePlain = boolSetting("write-plain", false);
    next.extraFolder = stringSetting("extra-folder", "");
    next.savePartial = boolSetting("save-partial", true);

    next.autoPlay = boolSetting("auto-play", false);

    next.showHud = boolSetting("show-hud", true);
    next.hudY = std::clamp(floatSetting("hud-y", 0.82), 0.1, 0.98);
    next.hudScale = std::clamp(floatSetting("hud-scale", 1.0), 0.5, 2.0);

    next.debugLog = boolSetting("debug-log", false);

    m_snapshot = std::move(next);
}

} // namespace mm
