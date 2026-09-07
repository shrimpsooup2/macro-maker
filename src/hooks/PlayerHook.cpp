#include "game/Engine.hpp"
#include "game/SlopeProbe.hpp"
#include "gen/Generator.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

// The run's clock, and the player's own hands.
//
// PlayerObject::update is the only place the game says "a physics step happened to
// player one", and pushButton and releaseButton are where input really lands: watching
// the game layer's handleButton instead misses most of it, because input arrives queued
// and is applied to the player later.
struct MacroPlayer : geode::Modify<MacroPlayer, PlayerObject> {
    void update(float dt) {
        PlayerObject::update(dt);

        auto* play = PlayLayer::get();
        if (play && play->m_player1 == this) {
            mm::Engine::get().notePlayerStep(this->getPositionX());
            mm::slopeProbeStep(play);
        }
    }

    bool pushButton(PlayerButton button) {
        bool result = PlayerObject::pushButton(button);
        auto* play = PlayLayer::get();
        if (play && play->m_player1 == this && button == PlayerButton::Jump) {
            mm::Generator::get().noteButton(true);
        }
        return result;
    }

    bool releaseButton(PlayerButton button) {
        bool result = PlayerObject::releaseButton(button);
        auto* play = PlayLayer::get();
        if (play && play->m_player1 == this && button == PlayerButton::Jump) {
            mm::Generator::get().noteButton(false);
        }
        return result;
    }
};
