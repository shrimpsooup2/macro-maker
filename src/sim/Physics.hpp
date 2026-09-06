#pragma once

// The 2.2 physics, as measured rather than as guessed at.
//
// Every constant in here was recorded from Geometry Dash 2.2081 while it ran, across
// 441,488 physics steps and 2,017 orb and pad activations, by the Physics Lab mod. The
// integration rule was derived the same way. Nothing is inherited from another
// simulator, and the Python original this is ported from is checked frame by frame
// against real recordings: velocity error 0.000000 over tens of thousands of steps for
// cube, ball, swing, wave, spider mini and UFO mini.
//
// Velocity is per 60fps frame and dt is in frame units, so one physics step at the
// game's 240 steps per second is dt = 0.25.

namespace mm {

enum Mode : int {
    Cube = 0,
    Ship = 1,
    Ball = 2,
    Ufo = 3,
    Wave = 4,
    Robot = 5,
    Spider = 6,
    Swing = 7,
    ModeCount = 8,
};

char const* modeName(int mode);

// y[i+1] = y[i] + v[i+1] * dt * kPositionScale, established at 99% exactness.
inline constexpr double kPositionScale = 0.9;

// The wave is the exception, and not a small one: it moves the whole of its velocity
// each step, and a mini wave moves twice it. Measured over 12,026 and 4,643 airborne
// steps respectively. At 0.9 a wave sinks about a block below where the game puts it
// every half second.
inline constexpr double kWavePositionScale = 1.0;
inline constexpr double kWaveMiniPositionScale = 2.0;

inline constexpr double kStepDt = 0.25;             // 240 physics steps per second
inline constexpr double kStepsPerSecond = 240.0;

// Ship and UFO both switch to a gentler acceleration once already moving upward fast
// enough. Below the threshold one value dominates 96% of steps, above it the other
// does, with the changeover between velocity 1.75 and 2.00.
inline constexpr double kFlyThreshold = 1.8;

// X travel, units per second, per speed portal.
inline constexpr double kXSpeed[5] = {251.16008, 311.58009, 387.42014, 468.00014, 576.00020};

// The wave travels at a fixed slope set by the speed portal, not by its size.
inline constexpr double kWaveSlope[5] = {4.186, 5.193, 6.457, 7.800, 9.600};

double gravityFor(int mode, bool mini);
double impulseFor(int mode, bool mini);       // 0 when the mode has no launch
double clampFor(int mode, bool mini);         // terminal velocity
double positionScale(int mode, bool mini);

struct Player {
    double y = 105.0;
    double vel = 0.0;
    int mode = Cube;
    bool mini = false;
    bool upsideDown = false;
    int speed = 1;
    bool onGround = false;
    bool dashing = false;

    double gravity() const {
        double g = gravityFor(mode, mini);
        return upsideDown ? -g : g;
    }
};

// One physics step. `button` is whether the button is down for this step.
void step(Player& p, bool button, double dt = kStepDt);

// Whatever this mode does when the button goes down.
void launch(Player& p);

} // namespace mm
