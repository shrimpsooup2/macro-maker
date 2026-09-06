#pragma once

#include "game/Capture.hpp"
#include "sim/Search.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class PlayLayer;
class GJBaseGameLayer;

namespace mm {

enum class Phase {
    Idle,
    Resetting,      // asked the game to put the level back to the start
    Capturing,      // driving: warm the level up, then read it
    Searching,      // a worker thread is looking for a route
    ReplayReset,    // asked for a reset before playing a route back
    Replaying,      // driving: feeding the route into the real game
    Finished,
    Failed,
};

// The whole job, in order: read the level off the running game, search it in the
// measured simulator on a thread of its own, play the answer back through the real game
// to see whether it is true, and write it out as a macro.
//
// The interesting part is what happens when the real game disagrees. A death in play is
// not a failure of the search, it is a measurement: the spot is marked expensive and the
// level is searched again with that knowledge. The simulator is good enough to find
// routes and not good enough to be trusted, and this is how the two are reconciled.
class Generator {
public:
    static Generator& get();

    void attach(PlayLayer* layer);
    void detach();

    bool busy() const;
    Phase phase() const { return m_phase; }

    bool startMaking();
    bool startPlayback();
    void stop();

    // The game's own update ran this frame; anything that needs a normally running level
    // happens here.
    void normalFrame(GJBaseGameLayer* layer);

    // The game's update was swallowed: the level is ours to step by hand.
    void driveFrame();

    void onLevelReset();

    // While a route is being played back a death is information, not the player losing a
    // run, so the game is never told it happened.
    bool suppressingGameplay() const;

    std::string headline() const;
    std::string detail() const;
    double progress() const;

    std::vector<std::string> const& lastFiles() const { return m_files; }

private:
    Generator() = default;

    void launchSearch();
    void collectSearch();
    void joinWorker();

    void beginReplay(bool forVerification);
    void finishReplay(bool survived);
    void noteRealDeath();

    void writeOut(bool complete);
    void release();
    void fail(std::string why);
    void settle(std::string note);

    PlayLayer* m_layer = nullptr;
    Phase m_phase = Phase::Idle;

    std::unique_ptr<Level> m_level;
    CaptureReport m_capture;

    std::thread m_worker;
    std::atomic<bool> m_searchDone{false};
    std::atomic<bool> m_cancel{false};
    SearchProgress m_progress;
    SearchResult m_pending;
    SearchResult m_result;

    // What the real game has taught the search: cells where a replay actually died.
    AvoidMap m_known;
    int m_round = 0;

    std::vector<char> m_inputs;
    int m_replayAt = 0;
    bool m_replayVerifying = false;
    bool m_replayWarm = false;
    int m_startOffset = 0;

    // The last stretch of the real replay, so a death can make the approach to it
    // expensive rather than only the spot itself.
    std::vector<std::pair<float, float>> m_realPath;

    bool m_selfReset = false;
    bool m_finishedInGame = false;
    double m_diedAt = 0.0;

    std::vector<std::string> m_files;
    std::string m_note;
};

} // namespace mm
