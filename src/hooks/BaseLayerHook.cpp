#include "game/Engine.hpp"
#include "gen/Generator.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

struct MacroBaseLayer : geode::Modify<MacroBaseLayer, GJBaseGameLayer> {
    // A frozen level's update is dropped, which is how the game is held still while the
    // search thinks. Otherwise the game runs its own frames as it always does: its
    // physics rate is fixed and does not need our help, and every attempt to take that
    // over by hand ended with a level that would not move.
    void update(float dt) {
        auto& engine = mm::Engine::get();
        if (engine.shouldSwallowUpdate(this)) {
            mm::Generator::get().frozenFrame();
            return;
        }

        GJBaseGameLayer::update(dt);

        // Not on the extra updates a fast replay runs: those are inside a frame, not
        // frames of their own.
        if (!engine.inNestedUpdate()) mm::Generator::get().normalFrame(this);
    }

    // One physics step, at the point the game takes its queued input for that step. This
    // is where a macro is played from: it does not matter how many steps a frame turns
    // out to be worth, because every one of them comes through here and gets the button
    // the route asks for.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        mm::Generator::get().beforePhysicsStep();
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    // A person leaning on the jump key during a replay would put the run out of step
    // with the macro being checked, so only our own input gets through.
    void handleButton(bool down, int button, bool isPlayer1) {
        auto& engine = mm::Engine::get();
        if (engine.owns(this) && !engine.feedingInput() &&
            mm::Generator::get().holdingTheControls()) {
            return;
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
