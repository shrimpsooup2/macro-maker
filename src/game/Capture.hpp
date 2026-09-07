#pragma once

#include "sim/Level.hpp"

#include <memory>
#include <string>

class PlayLayer;

namespace mm {

struct CaptureReport {
    int objectsSeen = 0;
    int kept = 0;
    int hazards = 0;
    int solids = 0;
    int orbs = 0;
    int portals = 0;
    int droppedMoving = 0;
    int droppedTriggers = 0;
    int droppedScenery = 0;
    int droppedRiding = 0;
    bool hasDual = false;

    std::string summary() const;
};

// Build the simulator's picture of the level out of the running game.
//
// Every static level reader has to guess what an object id is and how big its hitbox
// really is, and guessing wrong is the largest error source in all of them. Nothing is
// guessed here: the type is the GameObjectType the game itself assigned when it built
// the level, and the box is the rect the game collides against, with that placement's
// own rotation and scale already in it.
std::unique_ptr<Level> captureLevel(PlayLayer* layer, CaptureReport& report);

// The state the game is in right now, written into the level as where a route starts.
void captureStartState(Level& level, PlayLayer* layer);

// Everything the simulator was given, as a CSV in the mod's folder. This is the only way
// to answer "did it read the level correctly" without guessing, so it is worth having
// when a route fails for no visible reason. Returns the file written.
std::string dumpCapture(Level const& level);

} // namespace mm
