#pragma once

#include "Physics.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The level as the simulator sees it: a flat list of boxes with a kind each.
//
// Unlike every static level reader, nothing here is guessed from the object id. The
// boxes are lifted straight off the running game -- real rects, with each placement's
// own rotation and scale already baked in, and the real GameObjectType the game
// assigned when it built the level. Guessing what an id is was the single largest error
// source in the Python reader this replaces, and it does not arise here.

namespace mm {

// GameObjectType, from the game's own enum.
enum Kind : int {
    KSolid = 0,
    KHazard = 2,
    KGravPortalDown = 3,
    KGravPortalUp = 4,
    KShipPortal = 5,
    KCubePortal = 6,
    KDecoration = 7,
    KYellowPad = 8,
    KPinkPad = 9,
    KGravPad = 10,
    KYellowOrb = 11,
    KPinkOrb = 12,
    KGravOrb = 13,
    KBallPortal = 16,
    KBigPortal = 17,
    KMiniPortal = 18,
    KUfoPortal = 19,
    KModifier = 20,
    KBreakable = 21,
    KDualPortal = 23,
    KSoloPortal = 24,
    KSlope = 25,
    KWavePortal = 26,
    KRobotPortal = 27,
    KTeleport = 28,
    KGreenOrb = 29,
    KDropOrb = 32,
    KSpiderPortal = 33,
    KRedPad = 34,
    KRedOrb = 35,
    KDashOrb = 37,
    KGravDashOrb = 38,
    KSwingPortal = 41,
    KSpiderOrb = 43,
    KSpiderPad = 44,
    KAnimatedHazard = 47,
};

bool isBlocking(int kind);
bool isDeadly(int kind);
bool isOrb(int kind);
bool isPad(int kind);
bool isPortalMode(int kind);        // one of the eight gamemode portals
int modeOfPortal(int kind);         // -1 when the kind is not a gamemode portal
int speedOfId(int objectId);        // -1 when the id is not a speed portal

struct Obj {
    int id = 0;
    int kind = KDecoration;
    float x = 0.f;
    float y = 0.f;
    float w = 30.f;
    float h = 30.f;
    float rot = 0.f;

    // For a slope: which corner of the box holds the right angle, as a pair of signs in
    // x and y. The hypotenuse runs between the two corners either side of it, and that
    // hypotenuse is the surface a run rides. Both zero for everything else.
    int cornerX = 0;
    int cornerY = 0;

    // Teleport portal, key 54: how far above its own position the partner sits.
    float extra = 0.f;

    // Key 99, the editor's multi-activate box. Without it an orb fires once in a run
    // and never again.
    bool multi = false;

    // Saw blades are round and the game reports them as the square their picture fits
    // in. The difference is all in the corners, and the corners are exactly where it
    // matters: a saw sunk into a platform clips anything walking off the edge beside it
    // while the blade itself is nowhere near.
    //
    // The radius is the game's own, not half the box: a blade is a good deal smaller
    // than the square it is drawn in, so taking the box for it makes every saw in the
    // level bigger than it really is. Zero means the game does not treat this one as
    // round, and neither do we.
    float radius = 0.f;
    bool round = false;

    // Index into the run's spent list, or -1 for something that cannot be used up.
    int consumable = -1;

    float left() const { return x - w * 0.5f; }
    float right() const { return x + w * 0.5f; }
    float bottom() const { return y - h * 0.5f; }
    float top() const { return y + h * 0.5f; }
};

// The floor's top surface, in the game's own runtime coordinates. A resting cube sits
// at y=105 and is 30 tall, which puts the ground at 90. (The Python reader works in the
// level file's coordinates instead, where the same floor is at zero: the game's
// positions sit 90 above the numbers a level string stores.)
inline constexpr double kGroundTop = 90.0;

// Flying modes are held inside a band measured from the portal that started them, so a
// portal placed in mid air gives a band in mid air.
inline constexpr double kBandHeight = 300.0;

class Level {
public:
    std::vector<Obj> objects;       // sorted by x
    int consumableCount = 0;

    double length = 0.0;            // the game's own level length
    double ceiling = 3000.0;        // leaving vertically ends the run
    double floorLimit = -300.0;

    Player start;                   // the state the game itself starts the run in
    double startX = 0.0;

    // The player's box as the game itself reports it, for the mode the capture was taken
    // in. The one thing in this simulator that used to be a guess and no longer has to
    // be, since the game is right there to ask.
    double measuredWidth = 0.0;
    double measuredHeight = 0.0;
    int measuredMode = -1;
    bool measuredMini = false;

    std::string levelName;
    int levelId = 0;

    // Bookkeeping worth reporting: what the level has in it that the simulator cannot
    // model, so a route that fails has a named reason rather than a shrug.
    int movedObjects = 0;           // dropped: a trigger picks them up and moves them
    int triggerCount = 0;
    bool hasDual = false;

    void build();                   // buckets and limits, once the objects are in

    // Everything that could be touched around x. Not thread safe: the span cache means
    // one Level belongs to one thread at a time.
    std::vector<int> const& nearby(double x, double reach = 60.0) const;

private:
    int m_bucketBase = 0;
    std::vector<std::vector<int>> m_buckets;
    mutable std::unordered_map<std::uint64_t, std::vector<int>> m_spans;
    static std::vector<int> const s_empty;
};

} // namespace mm
