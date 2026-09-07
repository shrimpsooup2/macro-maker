#include "Engine.hpp"

#include "sim/Physics.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <cmath>

using namespace geode::prelude;

namespace mm {

namespace {

constexpr int kJumpButton = 1;

// The speed portal a given player speed belongs to. The game keeps the multiplier rather
// than the index, and the five values are far enough apart that the nearest is never in
// doubt.
constexpr float kSpeedValues[5] = {0.7f, 0.9f, 1.1f, 1.3f, 1.6f};

} // namespace

Engine& Engine::get() {
    static Engine instance;
    return instance;
}

void Engine::attach(PlayLayer* layer) {
    m_layer = layer;
    m_frozen = false;
    m_nested = false;
    m_held = false;
    m_feeding = false;
    m_died = false;
    m_completed = false;
    resetStepClock();
}

void Engine::detach() {
    unfreeze();
    m_layer = nullptr;
    m_held = false;
}

bool Engine::owns(GJBaseGameLayer* layer) const {
    return m_layer != nullptr && static_cast<GJBaseGameLayer*>(m_layer) == layer;
}

void Engine::freeze() {
    if (!m_layer || m_frozen) return;
    setHeld(false);
    m_frozen = true;
}

void Engine::unfreeze() {
    if (!m_layer || !m_frozen) return;
    setHeld(false);
    m_frozen = false;
}

bool Engine::shouldSwallowUpdate(GJBaseGameLayer* layer) const {
    return m_frozen && !m_nested && owns(layer);
}

void Engine::setHeld(bool held) {
    if (!m_layer || held == m_held) return;
    m_feeding = true;
    m_layer->handleButton(held, kJumpButton, true);
    m_feeding = false;
    m_held = held;
}

int Engine::extraUpdate(double dt) {
    if (!m_layer || m_nested) return -1;
    auto* player = m_layer->m_player1;
    if (!player || player->m_isDead) return -1;

    int before = m_movingSteps;
    m_nested = true;
    m_layer->update(static_cast<float>(dt));
    m_nested = false;
    return m_movingSteps - before;
}

void Engine::notePlayerStep(float x) {
    // Only a step that moved the run counts. The repeats are the game calling the
    // player's update more often than it advances anything, and the steps before the
    // level gets going are not part of the run at all.
    if (x > m_lastPlayerX + 0.01f) ++m_movingSteps;
    m_lastPlayerX = x;
}

void Engine::resetStepClock() {
    m_movingSteps = 0;
    m_lastPlayerX = -1e9f;
}

void Engine::resetLevel() {
    if (!m_layer) return;
    setHeld(false);
    m_layer->resetLevel();
    resetStepClock();
    m_died = false;
    m_completed = false;
}

void Engine::ensureLevelStarted() {
    if (!m_layer) return;
    if (!m_layer->m_started) m_layer->startGame();
}

bool Engine::levelStarted() const {
    return m_layer && m_layer->m_started;
}

float Engine::playerX() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    return player ? player->getPositionX() : 0.f;
}

float Engine::playerY() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    return player ? player->getPositionY() : 0.f;
}

double Engine::playerVelocityY() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    return player ? player->m_yVelocity : 0.0;
}

bool Engine::playerOnGround() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    return player && player->m_isOnGround;
}

bool Engine::playerIsDead() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    return player && player->m_isDead;
}

bool Engine::playerUpsideDown() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    return player && player->m_isUpsideDown;
}

bool Engine::playerMini() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    if (!player) return false;
    // Mini is not a flag on the player: it is the size the game scaled it to, 0.6 of
    // full, which is also the figure the physics recordings were taken at.
    return player->m_vehicleSize < 0.8f;
}

int Engine::playerMode() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    if (!player) return Cube;
    // The game's own names: a bird is the UFO and a dart is the wave.
    if (player->m_isShip) return Ship;
    if (player->m_isBird) return Ufo;
    if (player->m_isBall) return Ball;
    if (player->m_isDart) return Wave;
    if (player->m_isRobot) return Robot;
    if (player->m_isSpider) return Spider;
    if (player->m_isSwing) return Swing;
    return Cube;
}

int Engine::playerSpeed() const {
    auto* player = m_layer ? m_layer->m_player1 : nullptr;
    if (!player) return 1;

    float speed = player->m_playerSpeed;
    int best = 1;
    float closest = 1e9f;
    for (int index = 0; index < 5; ++index) {
        float gap = std::abs(speed - kSpeedValues[index]);
        if (gap < closest) {
            closest = gap;
            best = index;
        }
    }
    return best;
}

} // namespace mm
