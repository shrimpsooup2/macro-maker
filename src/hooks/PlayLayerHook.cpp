#include "game/Engine.hpp"
#include "gen/Generator.hpp"
#include "settings/Settings.hpp"
#include "ui/Hud.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

struct MacroPlayLayer : geode::Modify<MacroPlayLayer, PlayLayer> {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        mm::SettingsCache::get().refresh();
        mm::Engine::get().attach(this);
        mm::Generator::get().attach(this);
        mm::Hud::attachTo(this);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        // Whoever asked for the reset, the run's clock starts again with it.
        mm::Engine::get().resetStepClock();
        mm::Generator::get().onLevelReset();
    }

    // While a route is being driven a death is the game telling us the simulator was
    // wrong, not the player losing a run, so the game is never told it happened: the
    // level would restart underneath us and the answer would be lost.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        mm::Engine::get().notifyDeath();
        if (mm::Generator::get().suppressingGameplay()) return;
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        mm::Engine::get().notifyComplete();
        if (mm::Generator::get().suppressingGameplay()) return;
        PlayLayer::levelComplete();
    }

    void onQuit() {
        mm::Generator::get().detach();
        mm::Engine::get().detach();
        PlayLayer::onQuit();
    }
};
