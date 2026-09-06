#pragma once

#include "Level.hpp"
#include "Physics.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// One attempt at a level: collision, portals, orbs, pads, slopes and bands on top of the
// measured physics.
//
// Two things in here are inferred rather than measured, and are marked where they
// appear: the size of the player's own box, and the rule for when a side collision kills
// rather than merely stops you.

namespace mm {

// How much of the player has to be inside a hazard for it to be fatal. INFERRED.
inline constexpr double kKillBox = 0.34;

// A gravity portal does not leave the run's speed alone: it halves it. Measured over 49
// changes with nothing else in reach, across cube, ball, robot, spider, ship and ufo, 48
// of them land within a fiftieth of exactly half.
inline constexpr double kGravityPortalScale = 0.5;

// Standard orbs and pads at mini are exactly 0.8 of full size. The exceptions -- the
// black orb, and dash orbs in ship and UFO -- are handled where they arise.
inline constexpr double kMiniOrbScale = 0.8;

enum class Cause : int {
    None = 0,
    Hazard,
    Solid,
    LeftLevel,
};

// What killed a run, small enough to count in a table rather than format into a string.
struct Death {
    Cause cause = Cause::None;
    int objectId = 0;
    float x = 0.f;
    float y = 0.f;

    bool operator==(Death const& other) const {
        return cause == other.cause && objectId == other.objectId;
    }
};

std::string describe(Death const& death);

// The list of objects a run has used up. Shared between clones until one of them spends
// something, which keeps branching cheap: a beam clones a run per step and consumes
// something perhaps once in a hundred.
using Spent = std::shared_ptr<std::vector<std::uint8_t>>;

class Run {
public:
    explicit Run(Level const& level);

    Level const* level = nullptr;
    Player p;
    double x = 0.0;
    bool dead = false;
    bool finished = false;
    int frame = 0;

    // Set when a flying portal is entered: the band a flying mode is held inside is
    // measured from the portal that started it, not from where the run is now.
    bool hasBand = false;
    double bandCentre = 0.0;

    bool prevButton = false;

    // A press fires one orb and then has to be let go of before it fires another. It
    // can be made early -- holding from before an orb still catches it, which is the
    // buffer -- but it is spent once something takes it.
    bool pressSpent = false;

    Death death;

    // Advance one physics step. Returns whether the run is still going.
    bool step(bool button);

    // States this close together are interchangeable, so only the best need keeping.
    struct Key {
        std::int32_t yq = 0;
        std::int32_t bandq = 0;
        std::int16_t vq = 0;
        std::uint16_t flags = 0;

        bool operator==(Key const& other) const {
            return yq == other.yq && bandq == other.bandq && vq == other.vq &&
                   flags == other.flags;
        }
    };
    struct KeyHash {
        std::size_t operator()(Key const& key) const noexcept;
    };
    Key key() const;

    double progress() const;        // 0..1 through the level

    // The search asks these of a run it is deciding what to do with.
    void playerBox(double& width, double& height) const;
    bool overlaps(Obj const& o) const;
    bool isSpent(Obj const& o) const;

private:
    Spent m_spent;

    void takeCopyOfSpent();
    void markSpent(Obj const& o);
    void clearSpent(Obj const& o);

    void touchObjects(bool button, bool justPressed);
    void applyGravityPortal();
    void applyPad(Obj const& o);
    void applyOrb(Obj const& o);
    double dashVelocity(Obj const& o) const;
    bool spiderTeleport();
    void rideSlope(Obj const& o);
    void collide();
    void applyBand();

    bool touches(Obj const& o) const;
    bool innerHits(Obj const& o) const;

    double m_velBeforeStep = 0.0;
    double m_stepDelta = 0.0;
};

} // namespace mm
