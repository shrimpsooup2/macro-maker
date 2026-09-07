#include "Run.hpp"

#include <algorithm>
#include <cmath>

namespace mm {

namespace {

// Orb impulses, measured per mode and size, keyed by object id. The order is cube,
// ship, ball, ufo, wave, robot, spider, swing; a mode never recorded for an orb takes
// the cube value, which is what the recordings show it doing.
struct OrbRow {
    int id;
    double v[ModeCount];
};

constexpr OrbRow kOrbTable[] = {
    // yellow
    {36, {11.18, 11.18, 7.826, 11.18, 11.18, 10.062, 7.826, 11.18}},
    // pink
    {141, {8.05, 4.137, 6.026, 4.696, 8.05, 8.05, 5.635, 8.05}},
    // green
    {1022, {11.18, -7.826, 7.826, 11.18, 11.18, 11.18, 7.826, 11.18}},
    // blue
    {84, {-4.472, -4.472, -3.13, -4.472, -4.472, -4.472, -3.13, -4.472}},
    // black
    {1330, {-15.0, 14.0, -15.0, -11.2, -15.0, -15.0, -16.5, -15.0}},
    // red. Measured the other way round from the obvious guess: the game reported 1704
    // as a dash orb 223 times and 1333 as a red orb 77 times.
    {1333, {15.428, 11.18, 10.487, 11.404, 15.428, 14.31, 10.487, 15.428}},
    // dash
    {1704, {-11.88, 5.106, -8.256, -6.4, -11.88, -11.64, -8.643, -11.88}},
    // gravity dash
    {1751, {2.268, 2.268, 0.889, 0.901, 2.268, 2.268, 1.266, 2.268}},
};

struct PadRow {
    int kind;
    double v[ModeCount];
};

// The gravity pad is stored negative and halved, both corrections read off real play
// rather than off the impulse log: the log records the number the game hands its own
// function -- 12.8 for a cube -- but the run comes away with exactly half of it, moving
// towards the floor it just acquired.
constexpr PadRow kPadTable[] = {
    {KYellowPad, {16.0, 16.0, 9.6, 16.0, 16.0, 16.0, 9.6, 16.0}},
    {KPinkPad, {10.4, 5.6, 6.72, 6.4, 10.4, 10.4, 6.72, 10.4}},
    {KGravPad, {-6.4, -6.4, -3.84, -6.4, -6.4, -6.4, -3.84, -6.4}},
    {KRedPad, {20.0, 10.08, 12.0, 9.6, 20.0, 20.0, 12.0, 20.0}},
};

double lookupOrb(int objectId, int mode) {
    for (auto const& row : kOrbTable) {
        if (row.id == objectId) return row.v[mode];
    }
    return 0.0;
}

double lookupPad(int kind, int mode) {
    for (auto const& row : kPadTable) {
        if (row.kind == kind) return row.v[mode];
    }
    return 0.0;
}

bool isDashKind(int kind) {
    return kind == KDashOrb || kind == KGravDashOrb;
}

bool isFlipKind(int kind) {
    return kind == KGravOrb || kind == KGravDashOrb;
}

// Portals fire on entry and so have to arm again once left. Everything else stays spent
// for the run unless its multi-activate box is ticked.
bool rearms(Obj const& o) {
    return o.kind == KGravPortalDown || o.kind == KGravPortalUp || o.multi;
}

bool isFlyingMode(int mode) {
    return mode == Ship || mode == Ufo || mode == Wave || mode == Swing;
}

} // namespace

std::string describe(Death const& death) {
    switch (death.cause) {
        case Cause::Hazard:
            return "hazard id " + std::to_string(death.objectId);
        case Cause::Solid:
            return "ran into solid id " + std::to_string(death.objectId);
        case Cause::LeftLevel:
            return "left the level vertically";
        default:
            return "still going";
    }
}

std::size_t Run::KeyHash::operator()(Key const& key) const noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.yq)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.bandq)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint16_t>(key.vq)));
    mix(key.flags);
    return static_cast<std::size_t>(hash);
}

Run::Run(Level const& lvl) {
    level = &lvl;
    p = lvl.start;
    x = lvl.startX;
    if (lvl.consumableCount > 0) {
        m_spent = std::make_shared<std::vector<std::uint8_t>>(
            static_cast<std::size_t>(lvl.consumableCount), 0u);
    }
}

void Run::takeCopyOfSpent() {
    if (!m_spent) return;
    if (m_spent.use_count() > 1) {
        m_spent = std::make_shared<std::vector<std::uint8_t>>(*m_spent);
    }
}

bool Run::isSpent(Obj const& o) const {
    if (o.consumable < 0 || !m_spent) return false;
    return (*m_spent)[static_cast<std::size_t>(o.consumable)] != 0u;
}

void Run::markSpent(Obj const& o) {
    if (o.consumable < 0 || !m_spent) return;
    takeCopyOfSpent();
    (*m_spent)[static_cast<std::size_t>(o.consumable)] = 1u;
}

void Run::clearSpent(Obj const& o) {
    if (o.consumable < 0 || !m_spent) return;
    takeCopyOfSpent();
    (*m_spent)[static_cast<std::size_t>(o.consumable)] = 0u;
}

Run::Key Run::key() const {
    // Orbs spent were tried in here as well, on the grounds that two runs at the same
    // height having taken different orbs are not really the same. They are not, but
    // splitting on it costs more than it returns: it multiplies the state space and
    // thins the beam over every ordinary stretch.
    Key k;
    k.yq = static_cast<std::int32_t>(std::llround(p.y / 1.5));
    k.vq = static_cast<std::int16_t>(
        std::clamp<long long>(std::llround(p.vel * 8.0), -30000, 30000));
    k.bandq = hasBand ? static_cast<std::int32_t>(std::llround(bandCentre / 1.5)) : -2000000000;
    std::uint16_t flags = static_cast<std::uint16_t>(p.mode & 7);
    if (p.mini) flags |= 1u << 3;
    if (p.upsideDown) flags |= 1u << 4;
    if (p.onGround) flags |= 1u << 5;
    if (p.dashing) flags |= 1u << 6;
    flags |= static_cast<std::uint16_t>((p.speed & 7) << 7);
    k.flags = flags;
    return k;
}

double Run::progress() const {
    if (!level || level->length <= 1.0) return 0.0;
    return std::clamp(x / level->length, 0.0, 1.0);
}

void playerBoxFor(int mode, bool mini, double& width, double& height) {
    // INFERRED: 30 square at full size, 0.6 of that when mini (the size the recordings
    // report), and the wave is famously much smaller.
    double scale = mini ? 0.6 : 1.0;
    double side = (mode == Wave) ? 10.0 : 30.0;
    width = side * scale;
    height = side * scale;
}

void Run::playerBox(double& width, double& height) const {
    // The game's own box for the mode this level was captured in beats the guess.
    if (level && level->measuredMode == p.mode && level->measuredMini == p.mini &&
        level->measuredWidth > 0.0) {
        width = level->measuredWidth;
        height = level->measuredHeight;
        return;
    }
    playerBoxFor(p.mode, p.mini, width, height);
}

bool Run::overlaps(Obj const& o) const {
    double w = 0.0;
    double h = 0.0;
    playerBox(w, h);
    return (x - w * 0.5 < o.right() && x + w * 0.5 > o.left() && p.y - h * 0.5 < o.top() &&
            p.y + h * 0.5 > o.bottom());
}

bool Run::touches(Obj const& o) const {
    // Deliberately not the mode's own hitbox. A wave is a tenth the size of a cube, so
    // using the live hitbox means a wave portal shrinks the run out of reach of anything
    // placed beside it, and the run sails straight past the cube portal two units later.
    constexpr double half = 15.0;
    return (x - half < o.right() && x + half > o.left() && p.y - half < o.top() &&
            p.y + half > o.bottom());
}

bool Run::touchesHazard(Obj const& o) const {
    double w = 0.0;
    double h = 0.0;
    playerBox(w, h);
    double l = x - w * 0.5;
    double r = x + w * 0.5;
    double b = p.y - h * 0.5;
    double t = p.y + h * 0.5;

    if (o.round && o.radius > 0.f) {
        // A blade is a circle, and the nearest point of the player to its middle is what
        // decides whether the run is in it.
        double nearX = std::min(std::max<double>(o.x, l), r);
        double nearY = std::min(std::max<double>(o.y, b), t);
        double dx = nearX - o.x;
        double dy = nearY - o.y;
        return dx * dx + dy * dy <= static_cast<double>(o.radius) * o.radius;
    }

    return l < o.right() && r > o.left() && b < o.top() && t > o.bottom();
}

bool Run::innerHits(Obj const& o) const {
    // The smaller box the game uses to decide a collision is fatal rather than merely a
    // bump. Size is inferred, not measured. Without it, brushing a decorative sliver is
    // fatal: a 30x1.5 edge piece where a run spawns clips the full box on frame one.
    double w = 0.0;
    double h = 0.0;
    playerBox(w, h);
    w *= kKillBox;
    h *= kKillBox;
    double l = x - w * 0.5;
    double r = x + w * 0.5;
    double b = p.y - h * 0.5;
    double t = p.y + h * 0.5;

    if (!(l < o.right() && r > o.left() && b < o.top() && t > o.bottom())) return false;

    if (isDeadly(o.kind) && o.round) {
        // Nearest point of the player's own box to the middle of the blade, against the
        // radius the game gave that blade rather than half of the square it is drawn in.
        double nearX = std::min(std::max<double>(o.x, l), r);
        double nearY = std::min(std::max<double>(o.y, b), t);
        double radius = o.radius > 0.f ? static_cast<double>(o.radius) : o.w * 0.5;
        double dx = nearX - o.x;
        double dy = nearY - o.y;
        return dx * dx + dy * dy <= radius * radius;
    }
    return true;
}

bool Run::step(bool button) {
    if (dead || finished || !level) return false;

    bool justPressed = button && !prevButton;
    if (!button) pressSpent = false;
    if (justPressed) pressAirborne = !p.onGround;
    prevButton = button;
    ++frame;

    x += kXSpeed[std::clamp(p.speed, 0, 4)] / kStepsPerSecond;

    // Turn the button into whatever this mode does with it. Cube and robot launch off
    // the ground and will do it again the moment they land if the button is still down;
    // ball, spider and swing reverse gravity on a fresh press; the UFO launches in mid
    // air. Ship and wave have no discrete action -- their button is thrust and slope,
    // handled in the physics step.
    if (button && (p.mode == Cube || p.mode == Robot) && p.onGround) {
        launch(p);
    } else if (justPressed && p.mode == Spider) {
        spiderTeleport();
    } else if (justPressed && p.mode == Ball && p.onGround) {
        launch(p);
    } else if (justPressed && p.mode == Swing) {
        launch(p);
    } else if (justPressed && p.mode == Ufo) {
        launch(p);
    }

    // The velocity the step started with, kept because a gravity portal halves what the
    // run arrived with and then falls under the new gravity -- not the other way round.
    // A ball at -9.122 comes out at -4.432, which is half of -9.122 plus one flipped
    // step of ball gravity, and nothing else reproduces that number.
    m_velBeforeStep = p.vel;
    mm::step(p, button);
    m_stepDelta = p.vel - m_velBeforeStep;

    double cap = clampFor(p.mode, p.mini);
    p.vel = std::clamp(p.vel, -cap, cap);

    p.onGround = false;
    touchObjects(button, justPressed);
    if (dead) return false;
    collide();
    applyBand();

    if (p.y > level->ceiling || p.y < level->floorLimit) {
        dead = true;
        death = Death{Cause::LeftLevel, 0, static_cast<float>(x), static_cast<float>(p.y)};
        return false;
    }

    if (x >= level->length) finished = true;
    return !dead;
}

void Run::touchObjects(bool button, bool justPressed) {
    auto const& nearby = level->nearby(x);

    for (int index : nearby) {
        Obj const& o = level->objects[static_cast<std::size_t>(index)];

        if (isDeadly(o.kind)) {
            // No inner box for a hazard. That shrunken box came from a time when every
            // hitbox in here was a guess and a decorative sliver could kill you; the
            // boxes are the game's own now, and a spike is already 6 by 12 rather than
            // the 30 square it is drawn in. Asking for a third of the player to be
            // inside one on top of that is how a hazard laid flat into the ground gets
            // walked straight over.
            if (touchesHazard(o)) {
                dead = true;
                death = Death{Cause::Hazard, o.id, static_cast<float>(x),
                              static_cast<float>(p.y)};
                return;
            }
            continue;
        }

        if (!touches(o)) continue;

        int k = o.kind;
        if (k == KTeleport) {
            // Lands you at the partner's height, keeping your speed. Checked against
            // recorded runs: entries from y=8 to y=88 all come out within a couple of
            // units of the portal's own y plus its offset.
            if (!isSpent(o)) {
                markSpent(o);
                p.y = o.y + o.extra;
                p.onGround = false;
            }
        } else if (isPortalMode(k)) {
            p.mode = modeOfPortal(k);
            // A band is measured from the portal that started the mode, which is why a
            // portal placed mid air gives a band in a different place.
            hasBand = isFlyingMode(p.mode);
            bandCentre = o.y;
        } else if (k == KMiniPortal) {
            p.mini = true;
        } else if (k == KBigPortal) {
            p.mini = false;
        } else if (k == KGravPortalDown || k == KGravPortalUp) {
            // A portal fires when it is entered, once. Setting gravity again on every
            // frame the run is still inside it looks harmless, because setting it twice
            // to the same thing changes nothing -- until an orb is sitting in the
            // portal, which is how some levels are built. The orb flips gravity, the
            // portal sets it straight back the next frame, and the climb the level is
            // asking for cannot happen.
            if (!isSpent(o)) {
                markSpent(o);
                bool wantsUpsideDown = (k == KGravPortalDown);
                if (p.upsideDown != wantsUpsideDown) applyGravityPortal();
                p.upsideDown = wantsUpsideDown;
            }
        } else if (k == KModifier) {
            int speed = speedOfId(o.id);
            if (speed >= 0) p.speed = speed;
        } else if (isPad(o.kind)) {
            if (!isSpent(o)) {
                markSpent(o);
                applyPad(o);
            }
        } else if (isOrb(o.kind)) {
            // One orb per press, and the press has to be one an orb will take: made on
            // this step, or made earlier in the air and still held. A button held from
            // the ground -- a jump, in other words -- goes through an orb without
            // firing it, and a simulator that lets it fire plans routes nobody can play.
            if (button && (justPressed || pressAirborne) && !pressSpent &&
                (o.multi || !isSpent(o))) {
                pressSpent = true;
                if (!o.multi) markSpent(o);
                applyOrb(o);
            }
        }
    }

    // Let go of the things that are allowed to fire again. Keeping them spent for the
    // whole run meant bouncing back onto the same pad did nothing the second time,
    // which the game is happy to let you do.
    for (int index : nearby) {
        Obj const& o = level->objects[static_cast<std::size_t>(index)];
        if (o.consumable < 0 || !rearms(o)) continue;
        if (isSpent(o) && !touches(o)) clearSpent(o);
    }
}

void Run::applyGravityPortal() {
    // Halve the velocity the run came in with, then let the step fall the other way.
    // Doing it to the velocity as it stands after this step's own gravity would be half
    // a step out, which is visible as soon as a level flips at speed.
    if (p.mode == Wave) {
        p.vel *= kGravityPortalScale;       // re-set from the slope next step anyway
        return;
    }
    p.vel = m_velBeforeStep * kGravityPortalScale - m_stepDelta;
}

void Run::applyPad(Obj const& o) {
    // The spider pad throws the run at the opposite surface the way a spider click does,
    // rather than handing it a velocity. INFERRED from how it behaves in game: no
    // recording of one exists.
    if (o.kind == KSpiderPad) {
        spiderTeleport();
        return;
    }

    double v = lookupPad(o.kind, p.mode);
    if (p.mini) v *= kMiniOrbScale;
    if (o.kind == KGravPad) p.upsideDown = !p.upsideDown;
    p.vel = p.upsideDown ? -v : v;
    p.onGround = false;
}

void Run::applyOrb(Obj const& o) {
    if (o.kind == KSpiderOrb) {
        // Same as the spider pad: a teleport, not an impulse. INFERRED.
        spiderTeleport();
        return;
    }

    if (isFlipKind(o.kind)) p.upsideDown = !p.upsideDown;

    if (isDashKind(o.kind)) {
        p.vel = dashVelocity(o);
        p.onGround = false;
        p.dashing = true;
        return;
    }

    double v = lookupOrb(o.id, p.mode);
    if (p.mini && o.kind != KDropOrb) v *= kMiniOrbScale;   // black orb does not scale
    p.vel = p.upsideDown ? -v : v;
    p.onGround = false;
}

double Run::dashVelocity(Obj const& o) const {
    // A dash follows the way the orb is turned, not a stored number. The orb drags the
    // run along its own angle while the button is held, at the run's own travelling
    // speed, so the rise per frame is the distance covered per frame times the tangent
    // of that angle. Reading a measured impulse instead gives every dash orb in a level
    // the vertical kick of whichever one happened to be recorded.
    //
    // The game's rotation runs clockwise, so its sign is flipped to get an ordinary
    // angle. Velocity here is in screen terms, which is why gravity does not enter into
    // it: an upside-down run dashing up an orb still goes up.
    double angle = -static_cast<double>(o.rot) * 3.14159265358979323846 / 180.0;
    // Straight up is not a dash the game can express; treat anything near vertical as
    // the steepest diagonal rather than letting the tangent run away.
    double rise = std::tan(std::clamp(angle, -1.5, 1.5));
    double perFrame = kXSpeed[std::clamp(p.speed, 0, 4)] / 60.0;
    return perFrame * rise / positionScale(p.mode, p.mini);
}

bool Run::spiderTeleport() {
    // Spider does not jump: a click moves it straight to the first surface in the
    // direction gravity is about to point, and flips gravity.
    double w = 0.0;
    double h = 0.0;
    playerBox(w, h);
    bool goingUp = !p.upsideDown;
    bool found = false;
    double best = 0.0;

    for (int index : level->nearby(x, 40.0)) {
        Obj const& o = level->objects[static_cast<std::size_t>(index)];
        if (!isBlocking(o.kind)) continue;
        if (o.right() < x - w * 0.5 || o.left() > x + w * 0.5) continue;

        if (goingUp && o.bottom() >= p.y) {
            if (!found || o.bottom() < best) {
                best = o.bottom();
                found = true;
            }
        } else if (!goingUp && o.top() <= p.y) {
            if (!found || o.top() > best) {
                best = o.top();
                found = true;
            }
        }
    }

    if (!found) {
        if (goingUp) return false;
        best = kGroundTop;              // the level floor is always there below
    }

    p.y = goingUp ? best - h * 0.5 : best + h * 0.5;
    p.upsideDown = !p.upsideDown;
    p.vel = 0.0;
    p.onGround = true;
    p.dashing = false;
    return true;
}

void Run::rideSlope(Obj const& o) {
    // A ramp is climbed, not collided with. The game puts you on the sloping face and
    // carries you along it, and the speed the face is climbing at is still yours when it
    // runs out -- which is the whole point of a slope, and why running one off the end
    // launches you.
    if (o.cornerX == 0 || o.w <= 0.f || o.h <= 0.f) return;

    double w = 0.0;
    double h = 0.0;
    playerBox(w, h);
    if (x + w * 0.5 <= o.left() || x - w * 0.5 >= o.right()) return;

    bool flipped = p.upsideDown;
    // The face is only a floor if it is on the side gravity pulls the run towards.
    if ((o.cornerY > 0) != flipped) return;

    // Where the sloping face is at this x, and how fast it is climbing. cornerY says
    // which flat side the triangle sits on, cornerX which end the hypotenuse reaches
    // full height at.
    double clamped = std::clamp<double>(x, o.left(), o.right());
    double along = (o.cornerX > 0) ? (clamped - o.left()) / o.w : (o.right() - clamped) / o.w;
    double height = o.h * along;
    double surface = (o.cornerY < 0) ? (o.bottom() + height) : (o.top() - height);

    double rise = (static_cast<double>(o.h) / static_cast<double>(o.w)) *
                  (kXSpeed[std::clamp(p.speed, 0, 4)] / kStepsPerSecond);
    if (o.cornerX < 0) rise = -rise;
    if (o.cornerY > 0) rise = -rise;

    double foot = flipped ? p.y + h * 0.5 : p.y - h * 0.5;
    double into = flipped ? (foot - surface) : (surface - foot);
    if (into <= 0.0) return;            // above the face, nothing to stand on yet
    if (into > o.h + h) return;         // far below it: this is the level's underside

    p.y = flipped ? surface - h * 0.5 : surface + h * 0.5;
    p.vel = rise / (kStepDt * positionScale(p.mode, p.mini));
    double cap = clampFor(p.mode, p.mini);
    p.vel = std::clamp(p.vel, -cap, cap);
    p.onGround = true;
    p.dashing = false;
}

void Run::collide() {
    double w = 0.0;
    double h = 0.0;
    playerBox(w, h);

    // The level's own floor. It stops the run from both sides: only gravity pulling
    // downwards makes it something to stand on, but nothing gets to be underneath it
    // either way.
    if (p.y - h * 0.5 <= kGroundTop) {
        p.y = kGroundTop + h * 0.5;
        // Only a run moving into the floor is stopped by it. Zeroing the velocity
        // whatever it was cancelled anything fired from a standing start: a pad at
        // ground level set 16.0 and the same step set it straight back to nothing.
        if (!p.upsideDown) {
            if (p.vel <= 0.0) {
                p.vel = 0.0;
                p.onGround = true;
                p.dashing = false;
            }
        } else if (p.vel < 0.0) {
            p.vel = 0.0;
        }
    }

    for (int index : level->nearby(x)) {
        Obj const& o = level->objects[static_cast<std::size_t>(index)];
        if (o.kind == KSlope) {
            rideSlope(o);
            continue;
        }
        if (o.kind != KSolid && o.kind != KBreakable) continue;
        if (!overlaps(o)) continue;

        playerBox(w, h);
        double l = x - w * 0.5;
        double r = x + w * 0.5;
        double b = p.y - h * 0.5;
        double t = p.y + h * 0.5;

        // Everything here is in the run's own frame: down is wherever gravity pulls.
        // Upside down, landing means meeting a surface from underneath, and resolving to
        // the object's top would drive the run straight through it.
        bool flipped = p.upsideDown;
        bool falling = flipped ? (p.vel >= 0.0) : (p.vel <= 0.0);

        double toLanding = flipped ? (t - o.bottom()) : (o.top() - b);
        double toCeiling = flipped ? (o.top() - b) : (t - o.bottom());
        double fromSide = std::min<double>(r - o.left(), o.right() - l);

        if (falling && toLanding <= fromSide && toLanding <= toCeiling) {
            p.y = flipped ? (o.bottom() - h * 0.5) : (o.top() + h * 0.5);
            p.vel = 0.0;
            p.onGround = true;
            p.dashing = false;
        } else if (!falling && toCeiling <= fromSide) {
            p.y = flipped ? (o.top() + h * 0.5) : (o.bottom() - h * 0.5);
            p.vel = 0.0;
        } else if (fromSide < toLanding && fromSide < toCeiling && innerHits(o)) {
            // Only a genuine side-on hit is fatal, meaning the horizontal overlap is the
            // shallow one. Clipping the corner of a thin ledge on the way past is not
            // running into a wall, and treating it as one killed every run on a
            // staircase of 3-unit platforms.
            dead = true;
            death = Death{Cause::Solid, o.id, static_cast<float>(x), static_cast<float>(p.y)};
            return;
        }
        // Touching, but not deeply enough for the game to care. Nothing happens: pushing
        // the run clear instead leaves it wedged against decorative edges forever, which
        // is a worse lie than passing through them.
    }
}

void Run::applyBand() {
    if (!level || !level->useBands) return;
    if (!hasBand || !isFlyingMode(p.mode)) return;
    double half = kBandHeight * 0.5;
    double low = bandCentre - half;
    double high = bandCentre + half;
    if (p.y < low) {
        p.y = low;
        p.vel = 0.0;
    } else if (p.y > high) {
        p.y = high;
        p.vel = 0.0;
    }
}

} // namespace mm
