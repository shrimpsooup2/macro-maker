#include "Overlay.hpp"

#include "game/Engine.hpp"
#include "gen/Generator.hpp"
#include "settings/Settings.hpp"
#include "sim/Run.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace mm {

namespace {

constexpr char const* kOverlayId = "macro-overlay"_spr;
constexpr int kOverlayZ = 1000;

// How far either side of the run to draw. A whole level is tens of thousands of units
// and a screen is about six hundred.
constexpr float kReach = 700.f;

// How far the run has to travel before the picture is worth drawing again.
constexpr float kRedrawEvery = 90.f;

constexpr ccColor4F kSolid{0.55f, 0.75f, 1.0f, 0.9f};
constexpr ccColor4F kHazard{1.0f, 0.35f, 0.4f, 0.95f};
constexpr ccColor4F kSlope{0.5f, 1.0f, 0.6f, 0.9f};
constexpr ccColor4F kSpecial{1.0f, 0.85f, 0.3f, 0.95f};
constexpr ccColor4F kNothing{0.f, 0.f, 0.f, 0.f};
constexpr ccColor4F kPlanned{1.0f, 0.9f, 0.2f, 0.95f};
constexpr ccColor4F kReal{0.4f, 0.9f, 1.0f, 0.95f};
constexpr ccColor4F kGround{1.0f, 1.0f, 1.0f, 0.35f};
constexpr ccColor4F kGameBox{1.0f, 1.0f, 1.0f, 0.95f};
constexpr ccColor4F kKillBoxColour{1.0f, 0.55f, 0.2f, 0.95f};

ccColor4F colourFor(int kind) {
    if (isDeadly(kind)) return kHazard;
    if (kind == KSlope) return kSlope;
    if (isBlocking(kind)) return kSolid;
    return kSpecial;
}

void drawBox(CCDrawNode* draw, float left, float bottom, float right, float top,
             ccColor4F const& colour) {
    CCPoint corners[4] = {ccp(left, bottom), ccp(right, bottom), ccp(right, top),
                          ccp(left, top)};
    draw->drawPolygon(corners, 4, kNothing, 1.f, colour);
}

void drawPath(CCDrawNode* draw, std::vector<std::pair<float, float>> const& path, float around,
              ccColor4F const& colour) {
    for (std::size_t index = 1; index < path.size(); ++index) {
        auto const& from = path[index - 1];
        auto const& to = path[index];
        if (to.first < around - kReach || from.first > around + kReach) continue;
        draw->drawSegment(ccp(from.first, from.second), ccp(to.first, to.second), 1.2f, colour);
    }
}

bool g_visible = false;
bool g_asked = false;

} // namespace

bool overlayVisible() {
    if (!g_asked) {
        g_asked = true;
        g_visible = settings().showOverlay;
    }
    return g_visible;
}

void setOverlayVisible(bool visible) {
    g_asked = true;
    g_visible = visible;
}

Overlay* Overlay::get() {
    auto* play = PlayLayer::get();
    if (!play) return nullptr;
    return typeinfo_cast<Overlay*>(play->getChildByID(kOverlayId));
}

// Hung off the play layer, not off the layer the objects live in.
//
// Inside that one it drew in the level's own coordinates for free, which was tidy right
// up until it started disturbing the game's rendering: that tree is made of batch nodes,
// and a draw node in the middle of them breaks the batching they exist for. So it sits
// outside and copies that layer's position and scale every frame instead, which puts its
// drawing in exactly the same place without being part of it.
Overlay* Overlay::attachTo(PlayLayer* layer) {
    if (!layer) return nullptr;
    if (auto* existing = layer->getChildByID(kOverlayId)) {
        return typeinfo_cast<Overlay*>(existing);
    }

    auto* overlay = new (std::nothrow) Overlay();
    if (!overlay) return nullptr;
    if (!overlay->init()) {
        delete overlay;
        return nullptr;
    }
    overlay->autorelease();
    overlay->setID(kOverlayId);
    layer->addChild(overlay, kOverlayZ);
    return overlay;
}

bool Overlay::init() {
    if (!CCNode::init()) return false;

    m_draw = CCDrawNode::create();
    if (!m_draw) return false;
    this->addChild(m_draw);

    this->scheduleUpdate();
    return true;
}

void Overlay::update(float) {
    bool wanted = overlayVisible();
    this->setVisible(wanted);
    if (!wanted) return;

    // Follow the level's own layer, so what is drawn in level coordinates lands where
    // the level is on screen.
    auto* play = PlayLayer::get();
    if (!play || !play->m_objectLayer) return;
    this->setPosition(play->m_objectLayer->getPosition());
    this->setScale(play->m_objectLayer->getScale());
    this->setRotation(play->m_objectLayer->getRotation());

    Generator const& generator = Generator::get();
    float around = Engine::get().playerX();

    // Redrawing every frame would be pointless: the picture only changes when the level
    // is read again, when a route arrives, or when the run has travelled far enough that
    // a different part of the level is on screen.
    if (generator.drawVersion() == m_shownVersion &&
        std::abs(around - m_shownAround) < kRedrawEvery) {
        return;
    }
    m_shownVersion = generator.drawVersion();
    m_shownAround = around;
    redraw();
}

void Overlay::redraw() {
    if (!m_draw) return;
    m_draw->clear();

    Generator const& generator = Generator::get();
    float around = m_shownAround;

    // The floor the simulator believes in. If this line is not sitting on the ground the
    // game draws, nothing else on this picture means anything.
    m_draw->drawSegment(ccp(around - kReach, static_cast<float>(kGroundTop)),
                        ccp(around + kReach, static_cast<float>(kGroundTop)), 1.f, kGround);

    if (Level const* level = generator.capturedLevel()) {
        for (auto const& o : level->objects) {
            if (o.x < around - kReach) continue;
            if (o.x > around + kReach) break;      // objects are sorted by x
            if (o.round && o.radius > 0.f) {
                // A blade, drawn as the circle the game collides with rather than the
                // square it is drawn in.
                CCPoint ring[16];
                for (int point = 0; point < 16; ++point) {
                    double angle = 6.28318530718 * point / 16.0;
                    ring[point] = ccp(o.x + o.radius * static_cast<float>(std::cos(angle)),
                                      o.y + o.radius * static_cast<float>(std::sin(angle)));
                }
                m_draw->drawPolygon(ring, 16, kNothing, 1.f, colourFor(o.kind));
                continue;
            }
            drawBox(m_draw, o.left(), o.bottom(), o.right(), o.top(), colourFor(o.kind));
        }
    }

    // The band a flying mode would be held inside, when that is switched on at all.
    if (Level const* level = generator.capturedLevel()) {
        if (level->useBands) {
            for (auto const& o : level->objects) {
                if (o.x < around - kReach) continue;
                if (o.x > around + kReach) break;
                if (modeOfPortal(o.kind) < 0) continue;
                int mode = modeOfPortal(o.kind);
                if (mode != Ship && mode != Ufo && mode != Wave && mode != Swing) continue;
                float half = static_cast<float>(kBandHeight * 0.5);
                m_draw->drawSegment(ccp(o.x, o.y - half), ccp(o.x + 600.f, o.y - half), 0.8f,
                                    kGround);
                m_draw->drawSegment(ccp(o.x, o.y + half), ccp(o.x + 600.f, o.y + half), 0.8f,
                                    kGround);
            }
        }
    }

    drawPath(m_draw, generator.plannedPath(), around, kPlanned);
    drawPath(m_draw, generator.realPath(), around, kReal);

    // The player, three ways: the box the game collides with in white, the box the
    // simulator believes in over the top of it in yellow, and in orange the smaller one
    // that decides whether running into the side of a solid is fatal. Hazards are judged
    // on the yellow box now, not the orange one. If the white and the yellow are not the
    // same box, that difference is a bug.
    if (auto* play = PlayLayer::get()) {
        if (auto* player = play->m_player1) {
            auto const& rect = player->getObjectRect();
            drawBox(m_draw, rect.getMinX(), rect.getMinY(), rect.getMaxX(), rect.getMaxY(),
                    kGameBox);

            double width = 0.0;
            double height = 0.0;
            if (Level const* level = generator.capturedLevel()) {
                Run probe(*level);
                probe.p.mode = Engine::get().playerMode();
                probe.p.mini = Engine::get().playerMini();
                probe.playerBox(width, height);
            } else {
                playerBoxFor(Engine::get().playerMode(), Engine::get().playerMini(), width,
                             height);
            }

            float x = player->getPositionX();
            float y = player->getPositionY();
            drawBox(m_draw, x - static_cast<float>(width) * 0.5f,
                    y - static_cast<float>(height) * 0.5f, x + static_cast<float>(width) * 0.5f,
                    y + static_cast<float>(height) * 0.5f, kPlanned);

            float innerW = static_cast<float>(width * kKillBox) * 0.5f;
            float innerH = static_cast<float>(height * kKillBox) * 0.5f;
            drawBox(m_draw, x - innerW, y - innerH, x + innerW, y + innerH, kKillBoxColour);
        }
    }

    // Where each side thinks it went wrong.
    auto planned = generator.plannedEnd();
    if (planned.first != 0.f || planned.second != 0.f) {
        m_draw->drawDot(ccp(planned.first, planned.second), 6.f, kPlanned);
    }
    auto real = generator.realEnd();
    if (real.first != 0.f || real.second != 0.f) {
        m_draw->drawDot(ccp(real.first, real.second), 6.f, kHazard);
    }
}

} // namespace mm
