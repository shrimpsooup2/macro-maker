#include "SlopeProbe.hpp"

#include "Engine.hpp"
#include "sim/Level.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/file.hpp>

#include <cmath>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace mm {

namespace {

// How near a ramp counts as worth writing down.
constexpr float kNear = 120.f;

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
    }
}

void slopeProbeStep(PlayLayer* layer) {
    Probe& state = probe();
    if (state.ramps.empty() || state.count >= kMaxRows) return;
    if (!layer || !layer->m_player1) return;

    auto* player = layer->m_player1;
    float px = player->getPositionX();
    float py = player->getPositionY();
    ++state.step;

    // The nearest ramp, and only while the run is beside it.
    Ramp const* nearest = nullptr;
    float best = kNear;
    for (auto const& ramp : state.ramps) {
        float gap = std::abs(ramp.x - px);
        if (gap < best) {
            best = gap;
            nearest = &ramp;
        }
    }
    if (!nearest) return;

    auto const& rect = player->getObjectRect();
    if (state.rows.empty()) {
        state.rows =
            "step,x,y,vel,on_ground,mode,mini,speed,box_bottom,box_top,ramp_id,ramp_x,ramp_y,"
            "ramp_w,ramp_h,ramp_rot,flip_x,flip_y\n";
    }

    Engine& engine = Engine::get();
    state.rows += fmt::format(
        "{},{:.4f},{:.4f},{:.5f},{},{},{},{},{:.4f},{:.4f},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.1f},"
        "{},{}\n",
        state.step, px, py, player->m_yVelocity, player->m_isOnGround ? 1 : 0,
        modeName(engine.playerMode()), engine.playerMini() ? 1 : 0, engine.playerSpeed(),
        rect.getMinY(), rect.getMaxY(), nearest->id, nearest->x, nearest->y, nearest->w,
        nearest->h, nearest->rot, nearest->flipX ? 1 : 0, nearest->flipY ? 1 : 0);
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
