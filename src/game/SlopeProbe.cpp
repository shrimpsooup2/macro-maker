#include "SlopeProbe.hpp"

#include "Engine.hpp"
#include "sim/Level.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/file.hpp>

#include <cmath>
#include <map>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace mm {

namespace {

// How near a ramp counts as worth writing down.
constexpr float kNear = 45.f;

// Enough for a good few passes at a slope without letting the file run away.
constexpr std::size_t kMaxRows = 40000;

struct Ramp {
    int id = 0;
    float x = 0.f;
    float y = 0.f;
    float w = 30.f;
    float h = 30.f;
    float rot = 0.f;
    bool flipX = false;
    bool flipY = false;
};

struct Probe {
    std::vector<Ramp> ramps;
    std::string rows;
    std::size_t count = 0;
    int step = 0;
};

Probe& probe() {
    static Probe state;
    return state;
}

} // namespace

void slopeProbeReset(PlayLayer* layer) {
    Probe& state = probe();
    state.ramps.clear();
    state.rows.clear();
    state.count = 0;
    state.step = 0;
    if (!layer || !layer->m_objects) return;

    for (auto* object : CCArrayExt<GameObject*>(layer->m_objects)) {
        if (!object) continue;
        if (static_cast<int>(object->m_objectType) != KSlope) continue;

        auto const& rect = object->getObjectRect();
        if (rect.size.width <= 0.f || rect.size.height <= 0.f) continue;

        Ramp ramp;
        ramp.id = object->m_objectID;
        ramp.w = rect.size.width;
        ramp.h = rect.size.height;
        ramp.x = rect.origin.x + ramp.w * 0.5f;
        ramp.y = rect.origin.y + ramp.h * 0.5f;
        ramp.rot = object->getRotation();
        ramp.flipX = object->m_isFlipX;
        ramp.flipY = object->m_isFlipY;
        state.ramps.push_back(ramp);
    }

    if (!state.ramps.empty()) {
        log::info("[macro-maker] {} slopes in this level, ready to record what they do",
                  state.ramps.size());
        return;
    }

    // No slopes at all is worth saying, and worth saying what the level does have
    // instead: a level full of ramps that the game does not type as slopes would explain
    // rather a lot, and it cannot be told from a level that simply has none.
    std::map<int, int> types;
    for (auto* object : CCArrayExt<GameObject*>(layer->m_objects)) {
        if (!object) continue;
        ++types[static_cast<int>(object->m_objectType)];
    }
    std::string summary;
    for (auto const& [type, count] : types) {
        if (!summary.empty()) summary += ", ";
        summary += fmt::format("type {} x{}", type, count);
    }
    log::info("[macro-maker] no slopes in this level. What it has: {}", summary);
}

void slopeProbeStep(PlayLayer* layer) {
    Probe& state = probe();
    if (state.ramps.empty() || state.count >= kMaxRows) return;
    if (!layer || !layer->m_player1) return;

    auto* player = layer->m_player1;
    float px = player->getPositionX();
    float py = player->getPositionY();
    ++state.step;

    auto const& rect = player->getObjectRect();

    // The ramp the run is actually on, which means near in both directions. Picking the
    // nearest by how far along the level it is logged a great many steps spent thirty
    // units underneath a ramp the run never touched, and those say nothing about riding.
    Ramp const* nearest = nullptr;
    float best = 1e9f;
    for (auto const& ramp : state.ramps) {
        float gapX = std::abs(ramp.x - px) - ramp.w * 0.5f;
        float gapY = std::abs(ramp.y - py) - ramp.h * 0.5f;
        if (gapX > kNear || gapY > kNear) continue;
        float gap = std::max(gapX, 0.f) + std::max(gapY, 0.f);
        if (gap < best) {
            best = gap;
            nearest = &ramp;
        }
    }
    if (!nearest) return;

    // Where the run's feet are against that ramp's box, and how far across it they are:
    // between them those say whether it is riding the face, standing on the flat, or
    // going past underneath.
    float footAbove = rect.getMinY() - (nearest->y - nearest->h * 0.5f);
    float acrossFrom = px - (nearest->x - nearest->w * 0.5f);
    if (state.rows.empty()) {
        state.rows =
            "step,x,y,vel,on_ground,mode,mini,speed,box_bottom,box_top,across_ramp,foot_above,"
            "ramp_id,ramp_x,ramp_y,ramp_w,ramp_h,ramp_rot,flip_x,flip_y\n";
    }

    Engine& engine = Engine::get();
    state.rows += fmt::format(
        "{},{:.4f},{:.4f},{:.5f},{},{},{},{},{:.4f},{:.4f},{:.4f},{:.4f},{},{:.2f},{:.2f},{:.2f},"
        "{:.2f},{:.1f},{},{}\n",
        state.step, px, py, player->m_yVelocity, player->m_isOnGround ? 1 : 0,
        modeName(engine.playerMode()), engine.playerMini() ? 1 : 0, engine.playerSpeed(),
        rect.getMinY(), rect.getMaxY(), acrossFrom, footAbove, nearest->id, nearest->x, nearest->y,
        nearest->w, nearest->h, nearest->rot, nearest->flipX ? 1 : 0, nearest->flipY ? 1 : 0);
    ++state.count;
}

std::string slopeProbeFlush() {
    Probe& state = probe();
    if (state.rows.empty() || state.count == 0) return "";

    auto* mod = Mod::get();
    if (!mod) return "";

    auto path = mod->getSaveDir() / "slopes.csv";
    auto written = utils::file::writeString(path, state.rows);
    state.rows.clear();
    state.count = 0;
    if (written.isErr()) {
        log::warn("[macro-maker] could not write the slope recording: {}", written.unwrapErr());
        return "";
    }

    log::info("[macro-maker] wrote what the slopes did to {}", path.string());
    return path.string();
}

} // namespace mm
