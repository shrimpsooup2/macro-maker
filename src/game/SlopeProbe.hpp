#pragma once

#include <string>

class PlayLayer;

namespace mm {

// Writing down what the game does on a slope, so the simulator can be built on that
// rather than on a guess.
//
// Slopes are the one mechanic in here nobody measured: the Python reference says outright
// that they are treated as ordinary blocks, and what replaced that was reasoning about
// what a ramp ought to do. Reasoning is how a run ended up being hauled onto a face it
// never touched and launched off it.
//
// So this records every step spent near one: where the run is, how fast, whether the game
// says it is on the ground, and where the ramp's own surface is underneath it. Ride a
// slope a few times, and the numbers say what riding one does -- the height it holds you
// at, the speed it carries, and what you leave the end of it with.
void slopeProbeReset(PlayLayer* layer);
void slopeProbeStep(PlayLayer* layer);

// Writes what has been collected to slopes.csv in the mod's folder, and starts again.
std::string slopeProbeFlush();

} // namespace mm
