#include "Capture.hpp"

#include "Engine.hpp"

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

// Saw blades are round and the game reports them as the square their picture fits in.
// The object's own radius says so outright where it is set; where it is not, the shape
// does: the game reports saws as large and square, spikes as small and taller than wide.
bool looksRound(GameObject* object, float w, float h) {
    if (object->m_objectRadius > 0.f) return true;
    return w >= 30.f && std::abs(w - h) <= 0.15f * w;
}

} // namespace

std::string CaptureReport::summary() const {
    return fmt::format("{} objects: {} solid, {} deadly, {} orbs and pads, {} portals; "
                       "dropped {} scenery, {} triggers, {} moved by triggers",
                       objectsSeen, solids, hazards, orbs, portals, droppedScenery,
                       droppedTriggers, droppedMoving);
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

    for (auto* object : CCArrayExt<GameObject*>(layer->m_objects)) {
        if (!object) continue;
        ++report.objectsSeen;

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
        entry.multi = object->m_isMultiActivate;

        if (kind == KSlope) {
            slopeCorner(entry.rot, object->m_isFlipX, object->m_isFlipY, entry.cornerX,
                        entry.cornerY);
        }
        if (isDeadly(kind)) entry.round = looksRound(object, entry.w, entry.h);
        if (kind == KTeleport) {
            if (auto* portal = typeinfo_cast<TeleportPortalObject*>(object)) {
                entry.extra = portal->m_teleportYOffset;
            }
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
}

} // namespace mm
