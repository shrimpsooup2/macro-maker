#include "Physics.hpp"

namespace mm {

namespace {

// (mode, mini) -> downward acceleration per frame unit. Mini is not one scale factor,
// which is the whole reason it had to be measured: identical for cube, ball and spider,
// different for ship, UFO and robot.
constexpr double kGravity[ModeCount][2] = {
    /* cube   */ {-0.864, -0.864},
    /* ship   */ {-0.276, -0.324},
    /* ball   */ {-0.516, -0.516},
    /* ufo    */ {-0.344, -0.404},
    /* wave   */ {0.0, 0.0},
    /* robot  */ {-0.776, -0.760},
    /* spider */ {-0.516, -0.516},
    /* swing  */ {-0.344, -0.344},
};

// Velocity the mode leaves the ground with. Zero means the mode has no such launch:
// the ship's button is thrust and the wave's is a slope, both handled in step().
constexpr double kImpulse[ModeCount][2] = {
    /* cube   */ {10.964, 8.728},
    /* ship   */ {0.0, 0.0},
    /* ball   */ {3.483, 2.813},
    /* ufo    */ {6.871, 6.648},
    /* wave   */ {0.0, 0.0},
    /* robot  */ {5.590, 4.248},
    /* spider */ {1.129, 1.129},
    /* swing  */ {6.314, 6.314},
};

// Velocities the game refuses to exceed, measured.
constexpr double kClamp[ModeCount][2] = {
    /* cube   */ {15.0, 15.0},
    /* ship   */ {6.4, 7.529},
    /* ball   */ {15.0, 15.0},
    /* ufo    */ {6.4, 7.529},
    /* wave   */ {9.6, 9.6},
    /* robot  */ {15.0, 15.0},
    /* spider */ {15.0, 15.0},
    /* swing  */ {8.0, 8.0},
};

// (mode, mini) -> held below the threshold, held above, released below, released above.
// Only the two flying modes that accelerate have one.
constexpr double kFlyAccel[2][2][4] = {
    /* ship */ {{0.432, 0.344, -0.276, -0.412}, {0.508, 0.404, -0.324, -0.488}},
    /* ufo  */ {{-0.344, -0.516, -0.344, -0.516}, {-0.404, -0.608, -0.404, -0.608}},
};

int safeMode(int mode) {
    return (mode < 0 || mode >= ModeCount) ? Cube : mode;
}

} // namespace

char const* modeName(int mode) {
    switch (safeMode(mode)) {
        case Cube: return "cube";
        case Ship: return "ship";
        case Ball: return "ball";
        case Ufo: return "ufo";
        case Wave: return "wave";
        case Robot: return "robot";
        case Spider: return "spider";
        case Swing: return "swing";
        default: return "cube";
    }
}

double gravityFor(int mode, bool mini) {
    return kGravity[safeMode(mode)][mini ? 1 : 0];
}

double impulseFor(int mode, bool mini) {
    return kImpulse[safeMode(mode)][mini ? 1 : 0];
}

double clampFor(int mode, bool mini) {
    return kClamp[safeMode(mode)][mini ? 1 : 0];
}

double positionScale(int mode, bool mini) {
    if (safeMode(mode) == Wave) return mini ? kWaveMiniPositionScale : kWavePositionScale;
    return kPositionScale;
}

void step(Player& p, bool button, double dt) {
    double scale = positionScale(p.mode, p.mini);

    // A dash holds the trajectory rather than accelerating it: while the button stays
    // down the run keeps the velocity the orb gave it, and letting go drops it back
    // into ordinary physics. Modelled from how the orb behaves in game; there was no
    // dash in the recordings to measure against.
    if (p.dashing) {
        if (button) {
            p.y += p.vel * dt * scale;
            return;
        }
        p.dashing = false;
    }

    if (p.mode == Wave) {
        // A wave has no acceleration: holding sends it up its slope, releasing sends it
        // down the same slope.
        double slope = kWaveSlope[p.speed < 0 || p.speed > 4 ? 1 : p.speed];
        p.vel = button ? slope : -slope;
        if (p.upsideDown) p.vel = -p.vel;
    } else if (p.mode == Ship || p.mode == Ufo) {
        auto const& band = kFlyAccel[p.mode == Ship ? 0 : 1][p.mini ? 1 : 0];

        // The threshold is on velocity in the direction the run considers up, so it has
        // to be read the right way round when gravity is flipped.
        double upward = p.upsideDown ? -p.vel : p.vel;
        bool fast = upward > kFlyThreshold;

        double a = button ? (fast ? band[1] : band[0]) : (fast ? band[3] : band[2]);
        p.vel += (p.upsideDown ? -a : a) * dt;
    } else if (p.mode == Robot && button && !p.onGround) {
        // Measured: holding suspends gravity outright rather than adding thrust, so the
        // run keeps whatever velocity it launched with until release. Every simulator
        // that models robot as a cube with a second jump has this wrong.
    } else {
        p.vel += p.gravity() * dt;
    }

    p.y += p.vel * dt * scale;
}

void launch(Player& p) {
    double imp = impulseFor(p.mode, p.mini);
    if (imp == 0.0) return;

    if (p.mode == Ball || p.mode == Spider || p.mode == Swing) {
        // these reverse gravity rather than launching upward
        p.upsideDown = !p.upsideDown;
        p.vel = p.upsideDown ? -imp : imp;
    } else {
        p.vel = p.upsideDown ? -imp : imp;
    }
    p.onGround = false;
}

} // namespace mm
