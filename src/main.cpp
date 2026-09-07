#include "settings/Settings.hpp"
#include "ui/Actions.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

namespace {

void installHotkeys() {
    listenForKeybindSettingPresses(mm::kMakeKeySetting,
                                   [](Keybind const&, bool down, bool repeat, double) {
                                       if (!down || repeat) return false;
                                       mm::requestMake();
                                       return true;
                                   });

    listenForKeybindSettingPresses(mm::kPlayKeySetting,
                                   [](Keybind const&, bool down, bool repeat, double) {
                                       if (!down || repeat) return false;
                                       mm::requestPlay();
                                       return true;
                                   });

    listenForKeybindSettingPresses(mm::kRecordKeySetting,
                                   [](Keybind const&, bool down, bool repeat, double) {
                                       if (!down || repeat) return false;
                                       mm::requestRecord();
                                       return true;
                                   });

    listenForKeybindSettingPresses(mm::kStopKeySetting,
                                   [](Keybind const&, bool down, bool repeat, double) {
                                       if (!down || repeat) return false;
                                       mm::requestStop();
                                       return true;
                                   });
}

} // namespace

$on_mod(Loaded) {
    mm::SettingsCache::get().refresh();

    listenForAllSettingChanges(
        [](std::string_view, std::shared_ptr<SettingV3>) { mm::SettingsCache::get().refresh(); });

    installHotkeys();
}
