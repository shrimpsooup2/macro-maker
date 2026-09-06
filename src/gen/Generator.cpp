#include "Generator.hpp"

#include "game/Engine.hpp"
#include "macro/Macro.hpp"
#include "settings/Settings.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

namespace mm {

namespace {

using Clock = std::chrono::steady_clock;

// How many steps the level is allowed to take before it is moving. Two is normal.
constexpr int kWarmUpLimit = 60;

// Playing back at the game's own speed: four physics steps per drawn frame is 240 a
// second, which is exactly what the game runs at.
constexpr int kWatchStepsPerFrame = 4;

// A death in the real game is worth more than one in the simulator, because it is the
// only kind that is certainly true.
constexpr double kRealDeathCost = 900.0;
constexpr double kRealApproachCost = 400.0;

// How far back along the real run to make expensive. The approach is what has to change
// for the outcome to change; the spot itself is only where it ended.
constexpr double kApproachWindow = 1200.0;

std::string percentOf(double value, double total) {
    if (total <= 1.0) return "0%";
    return fmt::format("{:.1f}%", 100.0 * value / total);
}

} // namespace

Generator& Generator::get() {
    static Generator instance;
    return instance;
}

void Generator::attach(PlayLayer* layer) {
    detach();
    m_layer = layer;
    m_phase = Phase::Idle;
    m_note.clear();
    m_files.clear();
}

void Generator::detach() {
    m_cancel.store(true, std::memory_order_relaxed);
    joinWorker();

    Engine::get().endDriving();

    m_layer = nullptr;
    m_phase = Phase::Idle;
    m_level.reset();
    m_inputs.clear();
    m_realPath.clear();
    m_known.clear();
    m_round = 0;
}

bool Generator::busy() const {
    switch (m_phase) {
        case Phase::Idle:
        case Phase::Finished:
        case Phase::Failed:
            return false;
        default:
            return true;
    }
}

void Generator::joinWorker() {
    if (m_worker.joinable()) m_worker.join();
    m_searchDone.store(false, std::memory_order_relaxed);
}

bool Generator::startMaking() {
    if (!m_layer || busy()) return false;

    SettingsCache::get().refresh();

    m_known.clear();
    m_round = 0;
    m_files.clear();
    m_inputs.clear();
    m_realPath.clear();
    m_result = SearchResult{};
    m_level.reset();
    m_note.clear();
    m_finishedInGame = false;
    m_diedAt = 0.0;
    m_phase = Phase::Resetting;
    return true;
}

bool Generator::startPlayback() {
    if (!m_layer || busy()) return false;
    if (m_inputs.empty()) return false;

    m_replayVerifying = false;
    m_finishedInGame = false;
    m_phase = Phase::ReplayReset;
    return true;
}

void Generator::stop() {
    m_cancel.store(true, std::memory_order_relaxed);
    joinWorker();
    if (busy()) {
        m_phase = Phase::Failed;
        m_note = "stopped";
    }
    release();
}

bool Generator::suppressingGameplay() const {
    return Engine::get().driving();
}

void Generator::onLevelReset() {
    if (m_selfReset || !busy()) return;

    // The player restarted the level from under us, so whatever was going on is over.
    m_cancel.store(true, std::memory_order_relaxed);
    joinWorker();
    Engine::get().endDriving();
    m_phase = Phase::Failed;
    m_note = "the level restarted";
}

void Generator::normalFrame(GJBaseGameLayer* layer) {
    if (!m_layer || !Engine::get().owns(layer)) return;

    // The only thing that happens on a normally running frame is taking the level over.
    // Everything else -- resets included -- is done from inside the swallowed update, so
    // the level stays frozen from the moment a job starts until it is finished, rather
    // than playing itself into a wall while the search thinks.
    if (busy() && !Engine::get().driving()) Engine::get().beginDriving();
}

void Generator::driveFrame() {
    Engine& engine = Engine::get();

    if (m_phase == Phase::Resetting) {
        m_selfReset = true;
        engine.resetLevel();
        m_selfReset = false;
        m_phase = Phase::Capturing;
        return;
    }

    if (m_phase == Phase::Searching) {
        if (m_searchDone.load(std::memory_order_acquire)) collectSearch();
        return;
    }

    if (m_phase == Phase::ReplayReset) {
        m_selfReset = true;
        engine.resetLevel();
        m_selfReset = false;
        m_replayAt = 0;
        m_replayWarm = false;
        m_realPath.clear();
        m_phase = Phase::Replaying;
        return;
    }

    if (m_phase == Phase::Capturing) {
        int warm = engine.warmUpUntilMoving(kWarmUpLimit);
        if (warm < 0) {
            fail("the level would not start moving");
            return;
        }
        m_startOffset = warm;

        m_level = captureLevel(m_layer, m_capture);
        if (!m_level || m_level->objects.empty()) {
            fail("there is nothing in this level to read");
            return;
        }
        captureStartState(*m_level, m_layer);

        if (settings().debugLog) {
            log::info("{} {}", kLogTag, m_capture.summary());
            log::info("{} start: {} at x={:.0f} y={:.0f}, speed {}, length {:.0f}", kLogTag,
                      modeName(m_level->start.mode), m_level->startX, m_level->start.y,
                      m_level->start.speed, m_level->length);
            // The player and the objects have to be in the same coordinates for any of
            // this to mean anything. A cube standing on the ground reads y=105 and the
            // floor is taken to be at 90, so a start much below that says they are not.
            log::info("{} the level as the simulator sees it: {}", kLogTag,
                      dumpCapture(*m_level));
        }
        if (m_capture.hasDual) {
            log::warn("{} this level has a dual portal, which the simulator does not model",
                      kLogTag);
        }

        launchSearch();
        return;
    }

    if (m_phase != Phase::Replaying) {
        release();
        return;
    }

    if (!m_replayWarm) {
        int warm = engine.warmUpUntilMoving(kWarmUpLimit);
        if (warm < 0) {
            finishReplay(false);
            return;
        }
        m_replayWarm = true;
    }

    bool fast = m_replayVerifying && settings().verify == VerifyMode::Fast;
    auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                    std::chrono::duration<double, std::milli>(settings().verifyMs));

    for (int taken = 0;; ++taken) {
        if (!fast && taken >= kWatchStepsPerFrame) return;
        if (fast && taken > 0 && (taken & 7) == 0 && Clock::now() > until) return;

        if (m_replayAt >= static_cast<int>(m_inputs.size())) {
            finishReplay(true);
            return;
        }

        engine.setHeld(m_inputs[static_cast<std::size_t>(m_replayAt)] != 0);
        StepResult result = engine.stepOnce();
        ++m_replayAt;
        m_realPath.emplace_back(engine.playerX(), engine.playerY());

        if (result == StepResult::Finished) {
            m_finishedInGame = true;
            finishReplay(true);
            return;
        }
        if (result == StepResult::Dead) {
            m_diedAt = engine.playerX();
            finishReplay(false);
            return;
        }
    }
}

void Generator::launchSearch() {
    joinWorker();

    m_cancel.store(false, std::memory_order_relaxed);
    m_searchDone.store(false, std::memory_order_relaxed);
    m_progress.frame.store(0, std::memory_order_relaxed);
    m_progress.beamSize.store(0, std::memory_order_relaxed);
    m_progress.expansions.store(0, std::memory_order_relaxed);
    m_progress.x.store(0.0, std::memory_order_relaxed);
    m_progress.bestX.store(0.0, std::memory_order_relaxed);
    m_progress.round.store(0, std::memory_order_relaxed);

    Snapshot const& config = settings();
    SearchOptions options;
    options.width = config.beamWidth;
    options.every = config.flightInterval;
    options.seconds = config.searchSeconds;

    Level const* level = m_level.get();
    AvoidMap known = m_known;

    m_phase = Phase::Searching;
    m_worker = std::thread([this, level, options, known]() {
        SearchResult found = solveLearning(*level, options, known, &m_progress, &m_cancel);
        m_pending = std::move(found);
        m_searchDone.store(true, std::memory_order_release);
    });
}

void Generator::collectSearch() {
    joinWorker();
    m_result = std::move(m_pending);
    m_pending = SearchResult{};
    m_inputs = m_result.inputs;

    if (settings().debugLog) {
        log::info("{} round {}: {} at {} after {} states{}", kLogTag, m_round,
                  m_result.solved ? "solved" : "stopped",
                  percentOf(m_result.reachedX, m_result.length), m_result.expansions,
                  m_result.note.empty() ? "" : (", " + m_result.note));
        for (auto const& entry : m_result.deaths) {
            log::info("{}   {} x{}", kLogTag, describe(entry.death), entry.times);
        }
    }

    if (m_inputs.empty()) {
        fail("the search did not get anywhere");
        return;
    }

    if (settings().verify == VerifyMode::Off) {
        writeOut(m_result.solved);
        return;
    }

    m_replayVerifying = true;
    m_finishedInGame = false;
    m_phase = Phase::ReplayReset;
}

void Generator::finishReplay(bool survived) {
    // The level stays frozen between rounds: the next thing that happens to it is either
    // a reset for another replay, or being handed back to the player.
    if (!m_replayVerifying) {
        settle(survived ? "played the macro" : "the macro died in play");
        return;
    }

    if (survived) {
        if (m_result.solved && !m_finishedInGame) {
            // The route ran to its end without the level ending: the game and the
            // simulator disagree about where the run got to, which is drift rather than
            // a death. Worth saying, because the macro is still good as far as it goes.
            m_note = "the game ran the whole route but did not finish the level";
            log::warn("{} {} (simulator said {} of the level, game reached x={:.0f})", kLogTag,
                      m_note, percentOf(m_result.reachedX, m_result.length),
                      Engine::get().playerX());
        } else if (!m_result.solved) {
            m_note = fmt::format("the route holds up, and reaches {}",
                                 percentOf(m_result.reachedX, m_result.length));
        }
        writeOut(m_finishedInGame);
        return;
    }

    noteRealDeath();
    ++m_round;

    if (m_round > settings().verifyRounds) {
        m_note = fmt::format("the game killed it at {}", percentOf(m_diedAt, m_result.length));
        writeOut(false);
        return;
    }

    log::info("{} the game killed the route at x={:.0f} ({}), searching again (round {})",
              kLogTag, m_diedAt, percentOf(m_diedAt, m_result.length), m_round);
    launchSearch();
}

void Generator::noteRealDeath() {
    if (m_realPath.empty()) return;

    auto const& end = m_realPath.back();
    addAvoid(m_known, end.first, end.second, kRealDeathCost);

    // The approach as well as the spot. Anchoring only to where it died leaves the
    // search free to do exactly what it did last time until the final moment, and arrive
    // in the same state.
    double edge = end.first - kApproachWindow;
    for (auto const& point : m_realPath) {
        if (point.first >= edge) addAvoid(m_known, point.first, point.second, kRealApproachCost);
    }
}

void Generator::writeOut(bool complete) {
    if (!complete && !settings().savePartial) {
        fail("the route did not finish, and unfinished routes are turned off");
        return;
    }

    MacroInfo info;
    info.levelName = m_level ? m_level->levelName : std::string("Unknown");
    info.levelId = m_level ? m_level->levelId : 0;
    info.complete = complete;
    info.reachedPercent = complete ? 100.0
                                   : 100.0 * m_result.reachedX / std::max(1.0, m_result.length);
    info.startOffset = m_startOffset;

    MacroFormats formats;
    formats.gdr = settings().writeGdr;
    formats.mhr = settings().writeMhr;
    formats.plain = settings().writePlain;
    formats.extraFolder = settings().extraFolder;

    MacroFiles files = writeMacros(m_inputs, info, formats);
    if (!files.ok()) {
        fail(files.error);
        return;
    }

    m_files = files.written;

    std::string where = m_files.empty() ? std::string() : m_files.front();
    log::info("{} wrote {} macro file(s), first at {}", kLogTag, m_files.size(), where);

    std::string headline = complete ? "Macro written: full route"
                                    : fmt::format("Macro written: {} of the level",
                                                  percentOf(m_result.reachedX, m_result.length));
    Notification::create(headline, complete ? NotificationIcon::Success
                                            : NotificationIcon::Warning)
        ->show();

    if (m_note.empty()) {
        m_note = complete ? "the real game played it to the end"
                          : "kept the best route it had";
    }
    settle(m_note);

    if (settings().autoPlay) startPlayback();
}

// Handing the level back: put it at the start rather than leaving it wherever a replay
// stopped, since that is usually the player wedged inside whatever killed the route.
void Generator::release() {
    Engine& engine = Engine::get();
    if (engine.driving()) {
        m_selfReset = true;
        engine.resetLevel();
        m_selfReset = false;
        engine.endDriving();
    }
}

void Generator::fail(std::string why) {
    m_phase = Phase::Failed;
    m_note = std::move(why);
    release();
    log::warn("{} {}", kLogTag, m_note);
    Notification::create(m_note, NotificationIcon::Error)->show();
}

void Generator::settle(std::string note) {
    m_phase = Phase::Finished;
    m_note = std::move(note);
    release();
}

std::string Generator::headline() const {
    switch (m_phase) {
        case Phase::Resetting:
        case Phase::Capturing:
            return "Reading the level";
        case Phase::Searching:
            return m_round > 0 ? fmt::format("Searching again ({})", m_round) : "Searching";
        case Phase::ReplayReset:
        case Phase::Replaying:
            return m_replayVerifying ? "Checking the route" : "Playing the macro";
        case Phase::Finished:
            return "Done";
        case Phase::Failed:
            return "Stopped";
        case Phase::Idle:
        default:
            return "Macro Maker";
    }
}

std::string Generator::detail() const {
    double length = m_level ? std::max(1.0, m_level->length) : 1.0;

    switch (m_phase) {
        case Phase::Capturing:
        case Phase::Resetting:
            return "";
        case Phase::Searching: {
            double best = m_progress.bestX.load(std::memory_order_relaxed);
            long long states = m_progress.expansions.load(std::memory_order_relaxed);
            int width = m_progress.beamSize.load(std::memory_order_relaxed);
            return fmt::format("{} reached, beam {}, {}k states", percentOf(best, length), width,
                               states / 1000);
        }
        case Phase::Replaying: {
            float x = Engine::get().playerX();
            return fmt::format("{} of the way", percentOf(x, length));
        }
        case Phase::Finished:
        case Phase::Failed:
            return m_note;
        default:
            return m_inputs.empty() ? "" : fmt::format("{} steps ready", m_inputs.size());
    }
}

double Generator::progress() const {
    double length = m_level ? std::max(1.0, m_level->length) : 1.0;
    switch (m_phase) {
        case Phase::Searching:
            return std::clamp(m_progress.bestX.load(std::memory_order_relaxed) / length, 0.0, 1.0);
        case Phase::Replaying:
            return std::clamp(static_cast<double>(Engine::get().playerX()) / length, 0.0, 1.0);
        case Phase::Finished:
            return 1.0;
        default:
            return 0.0;
    }
}

} // namespace mm
