#include "game/Engine.hpp"
#include "gen/Generator.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

// While a job owns the level, the game's own stepping is replaced by ours. That is what
// lets the level be frozen while the search thinks, and stepped exactly one physics step
// at a time while a route is played back.
struct MacroBaseLayer : geode::Modify<MacroBaseLayer, GJBaseGameLayer> {
    void update(float dt) {
        auto& engine = mm::Engine::get();
        if (engine.shouldSwallowUpdate(this)) {
            mm::Generator::get().driveFrame();
            return;
        }
        GJBaseGameLayer::update(dt);
        mm::Generator::get().normalFrame(this);
    }

    // Every step a route is driven on has to be exactly the same length, or a macro
    // recorded on one machine's frame rate would land on different frames when it is
    // played back somewhere else.
    double getModifiedDelta(float dt) {
        auto& engine = mm::Engine::get();
        if (engine.shouldPinDelta(this)) return engine.stepDelta();
        return GJBaseGameLayer::getModifiedDelta(dt);
    }

    // A person leaning on the jump key during a replay would put the run out of step
    // with the macro being checked, so only our own input gets through.
    void handleButton(bool down, int button, bool isPlayer1) {
        auto& engine = mm::Engine::get();
        if (engine.driving() && engine.owns(this) && !engine.feedingInput()) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
