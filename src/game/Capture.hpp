#pragma once

#include "sim/Level.hpp"

#include <cstddef>
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
    int learnedHazards = 0;
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

// Ids the game has been seen to kill a run on.
//
// An object's type is usually enough to know it is lethal, and sometimes it is not: a
// hazard laid flat into the ground can come back as something this reads as scenery, and
// then the simulator plans a route straight through it. Nothing has to be guessed about
// it though, because the game names the object every time it kills somebody. What it
// names is remembered here and treated as deadly from then on, for the rest of the
// session and every level in it.
void rememberKiller(int objectId);
bool isKnownKiller(int objectId);
std::size_t knownKillerCount();

// How much of the player has to be inside a hazard before the game kills it.
//
// This is the last number in the simulator that is still a guess, and it is the one that
// decides whether a route clips the corner of a spike or dies on it. It cannot be read
// off the game directly, but it can be watched: every step the run overlaps a hazard and
// lives puts a floor under it, and the overlap at the moment of a death puts a ceiling on
// it. Play the level and the two numbers close in on the answer.
void watchHazardOverlap(Level const* level, float playerX, float playerY, bool died);
std::string hazardOverlapReport();

// How far onto a platform the run has to be before the game will let it jump.
//
// The simulator counts itself as standing the moment its box is over a block's top; the
// game plainly wants more than that, which is why a jump taken off the start of a
// platform happens earlier here than it does there. Every step says something: the game
// reports whether the run is on the ground, and the level says how far onto the block it
// is, so the least it has ever been while standing and the most it has ever been while
// still falling put the threshold between them.
void watchGroundContact(Level const* level, float playerX, float playerY, bool onGround);
std::string groundContactReport();

// How far a flying mode is allowed above and below the portal that started it.
//
// The simulator holds one inside three hundred units either side of that portal, which
// came out of scripted calibration runs that never went far enough to test it. Watching
// real play settles it: the highest and lowest a ship, UFO, wave or swing actually
// reaches, measured against the portal it came through.
void watchFlightBand(Level const* level, float playerX, float playerY, int mode);
std::string flightBandReport();

// The state the game is in right now, written into the level as where a route starts.
void captureStartState(Level& level, PlayLayer* layer);

// Everything the simulator was given, as a CSV in the mod's folder. This is the only way
// to answer "did it read the level correctly" without guessing, so it is worth having
// when a route fails for no visible reason. Returns the file written.
std::string dumpCapture(Level const& level);

} // namespace mm
