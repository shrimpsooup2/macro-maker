#include "game/Engine.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/binding/PlayLayer.hpp>

using namespace geode::prelude;

// The run's clock. This is the only place the game says "a physics step happened to
// player one", and everything the mod does is counted in those steps: where a capture
// was taken, which step each button change belongs to, where a replay has to pick the
// route up. Working the number out from the position instead would need the speed
// portal history and would be wrong the moment a level changed speed.
struct MacroPlayer : geode::Modify<MacroPlayer, PlayerObject> {
    void update(float dt) {
        PlayerObject::update(dt);

        auto* play = PlayLayer::get();
        if (play && play->m_player1 == this) {
            mm::Engine::get().notePlayerStep(this->getPositionX());
        }
    }
};
