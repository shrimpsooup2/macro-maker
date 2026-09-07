#include "game/Engine.hpp"
#include "game/SlopeProbe.hpp"
#include "gen/Generator.hpp"
#include "settings/Settings.hpp"
#include "ui/Hud.hpp"
#include "ui/Overlay.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>
#include <cmath>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

struct MacroPlayLayer : geode::Modify<MacroPlayLayer, PlayLayer> {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        mm::SettingsCache::get().refresh();
        mm::Engine::get().attach(this);
        mm::Generator::get().attach(this);
        mm::Hud::attachTo(this);
        mm::Overlay::attachTo(this);
        mm::slopeProbeReset(this);

        // Before this mod has done anything at all: is there something lethal sitting on
        // the spawn? If there is, it was not us that put it there.
        if (m_player1 && m_objects) {
            float x = m_player1->getPositionX();
            float y = m_player1->getPositionY();
            for (auto* object : CCArrayExt<GameObject*>(m_objects)) {
                if (!object) continue;
                int type = static_cast<int>(object->m_objectType);
                if (type != 2 && type != 47) continue;
                auto const& rect = object->getObjectRect();
                float ox = rect.origin.x + rect.size.width * 0.5f;
                float oy = rect.origin.y + rect.size.height * 0.5f;
                if (std::abs(ox - x) > 40.f || std::abs(oy - y) > 40.f) continue;
                log::warn("{} on opening the level there is already a hazard on the spawn: id {} "
                          "at ({:.1f}, {:.1f})",
                          mm::kLogTag, object->m_objectID, ox, oy);
            }
        }
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
            // Everything there is to know about whatever did it. A spike that sits on
            // the player and kills it on the first step is either something the level
            // really contains or something wearing an object's clothes, and the two look
            // identical until you ask where it is and how big it is.
            if (object) {
                auto const& rect = object->getObjectRect();
                // Two ways of asking where it is. The rect is a cached thing the game
                // keeps on the object; the position is the node's own. When a spike
                // reports itself at the spawn while the player is nowhere near the
                // spawn, one of these is stale, and knowing which decides whether the
                // phantom is in the level or in what this mod reads out of it.
                log::info("{} killed by id {} type {} | rect ({:.1f}, {:.1f}) {:.1f}x{:.1f} | "
                          "node ({:.1f}, {:.1f}) start ({:.1f}, {:.1f}) | player ({:.1f}, {:.1f}) "
                          "| radius {:.1f} notouch={} block1={} block2={}",
                          mm::kLogTag, object->m_objectID,
                          static_cast<int>(object->m_objectType),
                          rect.origin.x + rect.size.width * 0.5f,
                          rect.origin.y + rect.size.height * 0.5f, rect.size.width,
                          rect.size.height, object->getPositionX(), object->getPositionY(),
                          object->m_startPosition.x, object->m_startPosition.y,
                          player ? player->getPositionX() : -1.f,
                          player ? player->getPositionY() : -1.f, object->m_objectRadius,
                          object->m_isNoTouch ? 1 : 0,
                          object == m_player1CollisionBlock ? 1 : 0,
                          object == m_player2CollisionBlock ? 1 : 0);
            }
            mm::Generator::get().noteGameDeath(player ? player->getPositionX() : 0.f,
                                               player ? player->getPositionY() : 0.f,
                                               object ? object->m_objectID : -1);
        }
        mm::Engine::get().notifyDeath();
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        mm::slopeProbeFlush();
        mm::Generator::get().noteGameComplete();
        mm::Engine::get().notifyComplete();
        PlayLayer::levelComplete();
    }

    void onQuit() {
        mm::slopeProbeFlush();
        mm::Generator::get().detach();
        mm::Engine::get().detach();
        PlayLayer::onQuit();
    }
};
