#include "Engine.hpp"

#include "sim/Physics.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <cmath>

using namespace geode::prelude;

namespace mm {

namespace {

// Falling this far below the level, or being flung this far above it, is death in every
// level that exists. The game usually tells us first; this is the backstop.
constexpr float kFloorLimit = -800.f;
constexpr float kCeilingLimit = 20000.f;

constexpr int kJumpButton = 1;

// The speed portal a given player speed belongs to. The game keeps the multiplier
// rather than the index, and the five values are far enough apart that the nearest one
// is never in doubt.
constexpr float kSpeedValues[5] = {0.7f, 0.9f, 1.1f, 1.3f, 1.6f};

} // namespace

Engine& Engine::get() {
    static Engine instance;
    return instance;
}

void Engine::attach(PlayLayer* layer) {
    m_layer = layer;
    m_driving = false;
    m_inStep = false;
    m_held = false;
    m_died = false;
    m_completed = false;
    m_steps = 0;

    // Every level works its step length out afresh: it is a property of how this level
    // is running, not of the machine.
    m_calibrated = false;
    m_pinDelta = true;
    m_stepDelta = 1.0 / 240.0;
    m_stepsPerUpdate = 1;
    m_calibration.clear();
    resetStepClock();
}

void Engine::detach() {
    if (m_driving) endDriving();
    m_layer = nullptr;
    m_held = false;
}

bool Engine::owns(GJBaseGameLayer* layer) const {
    return m_layer != nullptr && static_cast<GJBaseGameLayer*>(m_layer) == layer;
}

void Engine::beginDriving() {
    if (!m_layer || m_driving) return;
    m_driving = true;
    m_died = false;
    m_completed = false;
}

void Engine::endDriving() {
    if (!m_layer || !m_driving) return;
    setHeld(false);
    m_driving = false;
}

bool Engine::shouldSwallowUpdate(GJBaseGameLayer* layer) const {
    return m_driving && !m_inStep && owns(layer);
}

bool Engine::shouldPinDelta(GJBaseGameLayer* layer) const {
    return m_pinDelta && m_inStep && owns(layer);
}

bool Engine::calibrateStep() {
    if (!m_layer) return false;
    if (m_calibrated) return true;

    // Pinned lengths first, since a fixed length is what makes a replay land on the same
    // frames every time. Letting the game work the delta out itself is the last resort:
    // it works, but its own accumulator decides where the step boundaries fall.
    struct Attempt {
        bool pin;
        double factor;
    };
    constexpr double kBase = 1.0 / 240.0;
    constexpr Attempt kAttempts[] = {
        {true, 1.0},  {true, 1.00002}, {true, 1.0005}, {true, 1.005}, {true, 1.05},
        {true, 1.25}, {true, 2.0},     {true, 4.0},    {false, 1.0},
    };
    constexpr int kCallsPerAttempt = 8;

    setHeld(false);
    m_calibration.clear();

    for (auto const& attempt : kAttempts) {
        m_pinDelta = attempt.pin;
        m_stepDelta = kBase * attempt.factor;

        int before = m_movingSteps;
        for (int call = 0; call < kCallsPerAttempt; ++call) {
            m_inStep = true;
            m_layer->update(static_cast<float>(m_stepDelta));
            m_inStep = false;
            ++m_steps;
            if (m_died || playerIsDead()) break;
        }
        int gained = m_movingSteps - before;

        log::info("[macro-maker] one update of {:.9f}s ({}) bought {} step(s) in {} calls",
                  m_stepDelta, attempt.pin ? "pinned" : "the game's own delta", gained,
                  kCallsPerAttempt);

        if (gained >= kCallsPerAttempt) {
            m_stepsPerUpdate = gained / kCallsPerAttempt;
            m_calibrated = true;
            m_calibration = fmt::format("{} {:.9f}s, {} step(s) per update",
                                        attempt.pin ? "pinned" : "free", m_stepDelta,
                                        m_stepsPerUpdate);
            if (m_stepsPerUpdate > 1) {
                log::warn("[macro-maker] the smallest update this game will take is {} physics "
                          "steps, so routes cannot be frame exact",
                          m_stepsPerUpdate);
            }
            return true;
        }

        if (m_died || playerIsDead()) {
            m_calibration = "the run died while working out the step length";
            return false;
        }
    }

    m_calibration = "no update length moved the run at all";
    m_pinDelta = true;
    m_stepDelta = kBase;
    return false;
}

void Engine::setHeld(bool held) {
    if (!m_layer || held == m_held) return;
    m_feeding = true;
    m_layer->handleButton(held, kJumpButton, true);
    m_feeding = false;
    m_held = held;
}

StepResult Engine::stepOnce() {
    if (!m_layer) return StepResult::Dead;

    m_died = false;
    m_completed = false;

    // getModifiedDelta is what normally counts this down, and driving goes around that
    // function, so it has to be counted down here or it would stay above zero for ever
    // and the game would quietly decline to move anything.
    if (m_layer->m_resumeTimer > 0) --m_layer->m_resumeTimer;

    m_inStep = true;
    m_layer->update(static_cast<float>(stepDelta()));
    m_inStep = false;

    ++m_steps;

    if (m_completed) return StepResult::Finished;
    if (m_died) return StepResult::Dead;

    auto* player = m_layer->m_player1;
    if (!player) return StepResult::Dead;
    if (player->m_isDead) return StepResult::Dead;

    float x = player->getPositionX();
    float y = player->getPositionY();
    if (!std::isfinite(x) || !std::isfinite(y)) return StepResult::Dead;
    if (y < kFloorLimit || y > kCeilingLimit) return StepResult::Dead;

    if (m_layer->m_levelLength > 1.f && x >= m_layer->m_levelLength) return StepResult::Finished;

    return StepResult::Alive;
}

void Engine::ensureLevelStarted() {
    if (!m_layer) return;
    if (!m_layer->m_started) m_layer->startGame();
}

bool Engine::levelStarted() const {
    return m_layer && m_layer->m_started;
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

int Engine::driveToStep(int target, int maxSteps) {
    if (!m_layer) return -1;

    setHeld(false);
    for (int taken = 0; taken < maxSteps; ++taken) {
        if (m_movingSteps >= target) return taken;
        if (stepOnce() != StepResult::Alive) return -1;
    }
    return m_movingSteps >= target ? maxSteps : -1;
}

void Engine::resetLevel() {
    if (!m_layer) return;
    setHeld(false);
    m_layer->resetLevel();
    resetStepClock();
    m_died = false;
    m_completed = false;
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
