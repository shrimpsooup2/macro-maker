#include "Capture.hpp"

#include "Engine.hpp"

#include "sim/Run.hpp"
#include "settings/Settings.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace mm {

namespace {

// Triggers that change where an object is or whether it is there at all. Move, toggle,
// rotate, scale and the two follows do; pulse and alpha only change how it looks, and an
// invisible block in this game is still a block.
bool movesThings(int objectId) {
    switch (objectId) {
        case 901:       // move
        case 1049:      // toggle
        case 1346:      // rotate
        case 2067:      // scale
        case 1347:      // follow
        case 3033:      // advanced follow
            return true;
        default:
            return false;
    }
}

// A slope is a right triangle filling its box, and which way it faces is decided by
// which corner holds the right angle. Unturned and unflipped that corner is the bottom
// right, so the hypotenuse runs from the bottom left up to the top right: the ordinary
// ramp you run up. Flips mirror the corner, and each quarter turn clockwise takes
// (x, y) to (y, -x).
void slopeCorner(float rotation, bool flipX, bool flipY, int& cornerX, int& cornerY) {
    int sx = 1;
    int sy = -1;
    if (flipX) sx = -sx;
    if (flipY) sy = -sy;

    int quarters = static_cast<int>(std::lround(rotation / 90.0f)) % 4;
    if (quarters < 0) quarters += 4;
    for (int turn = 0; turn < quarters; ++turn) {
        int nx = sy;
        int ny = -sx;
        sx = nx;
        sy = ny;
    }
    cornerX = sx;
    cornerY = sy;
}

// What the game collides with for a round object. It keeps the radius on the object, so
// there is nothing to work out from the shape: if it is set, this thing is a circle of
// that size, and if it is not, it is a box like everything else. Scale is applied the
// same way it is to the box.
float roundRadius(GameObject* object) {
    if (object->m_objectRadius <= 0.f) return 0.f;
    float scale = std::max(0.01f, std::abs(object->getScale()));
    return object->m_objectRadius * scale;
}

} // namespace

namespace {
std::unordered_set<int>& killers() {
    static std::unordered_set<int> known;
    return known;
}
} // namespace

void rememberKiller(int objectId) {
    if (objectId <= 0) return;
    if (killers().insert(objectId).second) {
        log::info("[macro-maker] id {} kills: remembering that, since the simulator has to be "
                  "told what the game already knows",
                  objectId);
    }
}

bool isKnownKiller(int objectId) {
    return killers().count(objectId) != 0;
}

std::size_t knownKillerCount() {
    return killers().size();
}

namespace {

struct OverlapWatch {
    double survived = 0.0;      // the deepest the run has been in a hazard and lived
    double killed = 1e9;        // the shallowest it has been in one and died
    int survivedId = 0;
    int killedId = 0;
    long long steps = 0;
};

OverlapWatch& watch() {
    static OverlapWatch state;
    return state;
}

// How far into a hazard the player is, as a fraction of its own box: zero for touching
// the edge, one for the middle of the player being inside it.
double overlapFraction(Obj const& o, double px, double py, double w, double h) {
    double overlapX = std::min(px + w * 0.5, static_cast<double>(o.right())) -
                      std::max(px - w * 0.5, static_cast<double>(o.left()));
    double overlapY = std::min(py + h * 0.5, static_cast<double>(o.top())) -
                      std::max(py - h * 0.5, static_cast<double>(o.bottom()));
    if (overlapX <= 0.0 || overlapY <= 0.0) return 0.0;
    return std::min(overlapX / w, overlapY / h);
}

} // namespace

void watchHazardOverlap(Level const* level, float playerX, float playerY, bool died) {
    if (!level) return;

    double w = 0.0;
    double h = 0.0;
    playerBoxFor(level->measuredMode >= 0 ? level->measuredMode : 0, level->measuredMini, w, h);
    if (level->measuredWidth > 0.0) {
        w = level->measuredWidth;
        h = level->measuredHeight;
    }

    double deepest = 0.0;
    int deepestId = 0;
    for (int index : level->nearby(playerX, 60.0)) {
        Obj const& o = level->objects[static_cast<std::size_t>(index)];
        if (!isDeadly(o.kind)) continue;
        double fraction = overlapFraction(o, playerX, playerY, w, h);
        if (fraction > deepest) {
            deepest = fraction;
            deepestId = o.id;
        }
    }

    OverlapWatch& state = watch();
    ++state.steps;
    if (died) {
        if (deepest > 0.0 && deepest < state.killed) {
            state.killed = deepest;
            state.killedId = deepestId;
        }
    } else if (deepest > state.survived) {
        state.survived = deepest;
        state.survivedId = deepestId;
    }
}

std::string hazardOverlapReport() {
    OverlapWatch const& state = watch();
    if (state.steps == 0) return "nothing watched yet";
    if (state.killed > 1.0) {
        return fmt::format("survived {:.0f}% into a hazard (id {}) and has not died in one yet",
                           state.survived * 100.0, state.survivedId);
    }
    return fmt::format("survived {:.0f}% into a hazard (id {}), died at {:.0f}% (id {}): the "
                       "fatal depth is between those",
                       state.survived * 100.0, state.survivedId, state.killed * 100.0,
                       state.killedId);
}

namespace {

struct GroundWatch {
    double leastStanding = 1e9;     // least of the block the run had under it while standing
    double mostFalling = 0.0;       // most it had under it while the game still said falling
    long long standing = 0;
    long long falling = 0;
};

GroundWatch& groundWatch() {
    static GroundWatch state;
    return state;
}

} // namespace

void watchGroundContact(Level const* level, float playerX, float playerY, bool onGround) {
    if (!level) return;

    double w = level->measuredWidth > 0.0 ? level->measuredWidth : 30.0;
    double h = level->measuredHeight > 0.0 ? level->measuredHeight : 30.0;
    double centre = playerY + level->measuredOffsetY;
    double bottom = centre - h * 0.5;

    // How much of a block is under the run, for whichever block it is standing on.
    double under = 0.0;
    for (int index : level->nearby(playerX, 60.0)) {
        Obj const& o = level->objects[static_cast<std::size_t>(index)];
        if (!isBlocking(o.kind)) continue;
        if (std::abs(bottom - static_cast<double>(o.top())) > 4.0) continue;
        double overlap = std::min(playerX + w * 0.5 - static_cast<double>(o.left()),
                                  static_cast<double>(o.right()) - (playerX - w * 0.5));
        if (overlap > under) under = overlap;
    }
    if (under <= 0.0) return;

    GroundWatch& state = groundWatch();
    if (onGround) {
        ++state.standing;
        if (under < state.leastStanding) state.leastStanding = under;
    } else {
        ++state.falling;
        if (under > state.mostFalling) state.mostFalling = under;
    }
}

std::string groundContactReport() {
    GroundWatch const& state = groundWatch();
    if (state.standing == 0) return "nothing watched yet";
    return fmt::format("the game stands on as little as {:.1f} units of block and is still "
                       "falling on as much as {:.1f} ({} steps standing, {} falling)",
                       state.leastStanding, state.mostFalling, state.standing, state.falling);
}

namespace {

struct BandWatch {
    double above = 0.0;
    double below = 0.0;
    int mode = -1;
    long long steps = 0;
};

BandWatch& bandWatch() {
    static BandWatch state;
    return state;
}

} // namespace

void watchFlightBand(Level const* level, float playerX, float playerY, int mode) {
    if (!level) return;
    if (mode != Ship && mode != Ufo && mode != Wave && mode != Swing) return;

    // The portal that started this stretch of flying is the last one of this mode behind
    // the run, which is the same thing the simulator anchors its band to.
    double centre = 1e9;
    for (auto const& o : level->objects) {
        if (o.x > playerX) break;
        if (modeOfPortal(o.kind) == mode) centre = o.y;
    }
    if (centre > 1e8) return;

    BandWatch& state = bandWatch();
    ++state.steps;
    state.mode = mode;
    double gap = playerY - centre;
    if (gap > state.above) state.above = gap;
    if (gap < state.below) state.below = gap;
}

std::string flightBandReport() {
    BandWatch const& state = bandWatch();
    if (state.steps == 0) return "no flying watched yet";
    return fmt::format("a {} has been {:.0f} above and {:.0f} below the portal that started it "
                       "over {} steps; the simulator allows {:.0f} either way",
                       modeName(state.mode), state.above, -state.below, state.steps,
                       kBandHeight * 0.5);
}

std::string CaptureReport::summary() const {
    return fmt::format("{} objects: {} solid, {} deadly, {} orbs and pads, {} portals; "
                       "dropped {} scenery, {} triggers, {} moved by triggers, {} riding "
                       "along with the player; {} known to kill despite their type",
                       objectsSeen, solids, hazards, orbs, portals, droppedScenery,
                       droppedTriggers, droppedMoving, droppedRiding, learnedHazards);
}

std::unique_ptr<Level> captureLevel(PlayLayer* layer, CaptureReport& report) {
    report = CaptureReport{};
    if (!layer || !layer->m_objects) return nullptr;

    auto level = std::make_unique<Level>();

    // Which groups something in this level picks up and moves. A block that a trigger
    // slides, turns, scales or switches off is not where the level says it is by the
    // time the run gets there, and nothing static can say where it actually is.
    std::unordered_set<int> shifted;
    for (auto* object : CCArrayExt<GameObject*>(layer->m_objects)) {
        if (!object) continue;
        if (!movesThings(object->m_objectID)) continue;
        auto* effect = typeinfo_cast<EffectGameObject*>(object);
        if (!effect) continue;
        if (effect->m_targetGroupID > 0) shifted.insert(effect->m_targetGroupID);
    }

    // Where the player is standing, so that anything the run is already inside can be
    // told apart from anything it has to avoid.
    float playerX = 0.f;
    float playerY = 0.f;
    float playerHalf = 15.f;
    if (auto* player = layer->m_player1) {
        playerX = player->getPositionX();
        playerY = player->getPositionY();
        playerHalf = 15.f * std::max(0.3f, player->m_vehicleSize);
    }

    for (auto* object : CCArrayExt<GameObject*>(layer->m_objects)) {
        if (!object) continue;
        ++report.objectsSeen;

        // The game keeps a block glued to each player for its collision-block triggers,
        // and it is in the object list like anything else. At the start of a run it sits
        // exactly where the player is, a step behind, and reads as a spike standing on
        // the run's head: every route died on its first step because of it.
        if (object == layer->m_player1CollisionBlock ||
            object == layer->m_player2CollisionBlock) {
            ++report.droppedRiding;
            continue;
        }

        int id = object->m_objectID;
        if (id <= 0) continue;

        // "No touch", set per instance. The game reports such an object as decoration
        // whatever its id normally is, which is why the same id can be a spike in one
        // level and scenery in another: it is not the id that varies, it is the
        // placement.
        if (object->m_isNoTouch) {
            ++report.droppedScenery;
            continue;
        }

        int kind = static_cast<int>(object->m_objectType);

        // Whatever the game says its type is, if it has killed somebody it is a hazard.
        if (!isDeadly(kind) && isKnownKiller(id)) {
            kind = KHazard;
            ++report.learnedHazards;
        }

        if (kind == KDecoration) {
            ++report.droppedScenery;
            continue;
        }
        if (kind == KDualPortal) report.hasDual = true;

        // The game files triggers under the same type as a speed portal, but a move or
        // pulse trigger is not something a run can touch, and a real level carries
        // hundreds of them stacked off the bottom of the screen.
        if (kind == KModifier && speedOfId(id) < 0) {
            ++report.droppedTriggers;
            continue;
        }

        if (!shifted.empty() && object->m_groups && object->m_groupCount > 0) {
            bool moved = false;
            int count = std::min<int>(object->m_groupCount, 10);
            for (int slot = 0; slot < count; ++slot) {
                int group = static_cast<int>((*object->m_groups)[static_cast<std::size_t>(slot)]);
                if (group > 0 && shifted.count(group)) {
                    moved = true;
                    break;
                }
            }
            if (moved) {
                ++report.droppedMoving;
                continue;
            }
        }

        CCRect const& rect = object->getObjectRect();
        if (rect.size.width <= 0.f || rect.size.height <= 0.f) {
            ++report.droppedScenery;
            continue;
        }

        Obj entry;
        entry.id = id;
        entry.kind = kind;
        entry.w = rect.size.width;
        entry.h = rect.size.height;
        entry.x = rect.origin.x + rect.size.width * 0.5f;
        entry.y = rect.origin.y + rect.size.height * 0.5f;
        entry.rot = object->getRotation();

        // Key 99, the editor's multi-activate box, is on EnhancedGameObject rather than
        // on GameObject: only the things that can be set off more than once carry it,
        // which is every orb and pad and nothing else we care about.
        if (auto* enhanced = typeinfo_cast<EnhancedGameObject*>(object)) {
            entry.multi = enhanced->m_isMultiActivate;
        }

        if (kind == KSlope) {
            slopeCorner(entry.rot, object->m_isFlipX, object->m_isFlipY, entry.cornerX,
                        entry.cornerY);
        }
        if (isDeadly(kind)) {
            entry.radius = roundRadius(object);
            entry.round = entry.radius > 0.f;
        }
        if (kind == KTeleport) {
            if (auto* portal = typeinfo_cast<TeleportPortalObject*>(object)) {
                entry.extra = portal->m_teleportYOffset;
            }
        }

        // Anything deadly that the player is standing in right now is not deadly: the
        // run is alive in this exact spot, which the game has just demonstrated. Only
        // something riding along with the player can be in that position, and it would
        // otherwise kill every route on its first step.
        if (isDeadly(kind) && entry.left() < playerX + playerHalf &&
            entry.right() > playerX - playerHalf && entry.bottom() < playerY + playerHalf &&
            entry.top() > playerY - playerHalf) {
            log::warn("[macro-maker] id {} is deadly and sits on the player at x={:.1f} y={:.1f}, "
                      "so it is being ignored",
                      id, entry.x, entry.y);
            ++report.droppedRiding;
            continue;
        }

        if (isBlocking(kind)) ++report.solids;
        else if (isDeadly(kind)) ++report.hazards;
        else if (isOrb(kind) || isPad(kind)) ++report.orbs;
        else ++report.portals;

        level->objects.push_back(entry);
        ++report.kept;
    }

    level->length = layer->m_levelLength;
    level->hasDual = report.hasDual;
    level->useBands = settings().flyingLimits;
    level->movedObjects = report.droppedMoving;
    level->triggerCount = report.droppedTriggers;

    if (layer->m_level) {
        level->levelName = std::string(layer->m_level->m_levelName);
        level->levelId = layer->m_level->m_levelID.value();
    }

    level->build();
    return level;
}

std::string dumpCapture(Level const& level) {
    std::string text = "id,kind,x,y,w,h,rot,cornerx,cornery,extra,multi,round\n";
    for (auto const& o : level.objects) {
        text += fmt::format("{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.1f},{},{},{:.2f},{},{}\n", o.id,
                            o.kind, o.x, o.y, o.w, o.h, o.rot, o.cornerX, o.cornerY, o.extra,
                            o.multi ? 1 : 0, o.round ? 1 : 0);
    }

    auto* mod = Mod::get();
    if (!mod) return "";
    auto path = mod->getSaveDir() / "last-capture.csv";
    auto written = utils::file::writeString(path, text);
    if (written.isErr()) {
        log::warn("[macro-maker] could not write the capture dump: {}", written.unwrapErr());
        return "";
    }
    return path.string();
}

void captureStartState(Level& level, PlayLayer* layer) {
    if (!layer) return;
    Engine& engine = Engine::get();

    level.startX = engine.playerX();
    level.start.y = engine.playerY();
    level.start.vel = engine.playerVelocityY();
    level.start.mode = engine.playerMode();
    level.start.mini = engine.playerMini();
    level.start.upsideDown = engine.playerUpsideDown();
    level.start.speed = engine.playerSpeed();
    level.start.onGround = engine.playerOnGround();
    level.start.dashing = false;

    if (auto* player = layer->m_player1) {
        auto const& rect = player->getObjectRect();
        if (rect.size.width > 0.f && rect.size.height > 0.f) {
            level.measuredWidth = rect.size.width;
            level.measuredHeight = rect.size.height;
            level.measuredMode = level.start.mode;
            level.measuredMini = level.start.mini;
            level.measuredOffsetY =
                rect.origin.y + rect.size.height * 0.5f - player->getPositionY();

            double guessWidth = 0.0;
            double guessHeight = 0.0;
            playerBoxFor(level.start.mode, level.start.mini, guessWidth, guessHeight);
            log::info("[macro-maker] the player's own box is {:.2f} by {:.2f} ({} {}), against "
                      "the {:.2f} by {:.2f} this was assuming; its middle sits {:.2f} from its "
                      "position",
                      rect.size.width, rect.size.height, modeName(level.start.mode),
                      level.start.mini ? "mini" : "full size", guessWidth, guessHeight,
                      rect.origin.y + rect.size.height * 0.5f - player->getPositionY());
        }
    }
}

} // namespace mm
