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

    // A death is reported and then allowed to happen.
    //
    // Swallowing it was the original idea -- keep the level from restarting under a
    // replay -- and it was the single worst bug in this mod. The game marks the player
    // dead before it calls this, so skipping the rest left that flag set with nothing to
    // ever clear it: every job afterwards saw a dead player on its first frame, which is
    // why replays "died" at x=5 before the route had started and why a recording ended
    // with nothing in it. The game is better at running its own level than we are.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (player == m_player1) {
            mm::Generator::get().noteGameDeath(player ? player->getPositionX() : 0.f,
                                               player ? player->getPositionY() : 0.f,
                                               object ? object->m_objectID : -1);
        }
        mm::Engine::get().notifyDeath();
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        mm::Generator::get().noteGameComplete();
        mm::Engine::get().notifyComplete();
        PlayLayer::levelComplete();
    }

    void onQuit() {
        mm::Generator::get().detach();
        mm::Engine::get().detach();
        PlayLayer::onQuit();
    }
};
