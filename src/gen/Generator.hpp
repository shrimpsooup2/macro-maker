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
    WaitingReset,   // the reset is queued, to happen between frames rather than inside one
    Starting,       // letting the game run the opening of the level itself
    Searching,      // the level is frozen while a worker thread looks for a route
    ReplayReset,    // asked for a reset before playing a route back
    Replaying,      // the route is being fed into the real game, step by step
    Recording,      // writing down what the person at the keyboard is doing
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
    bool startRecording();
    void stop();

    // The player pressed or let go of the jump button. Recording this rather than the
    // route is what says whether a macro that does not work is mistimed or is simply a
    // bad route: a recording of a run that already worked has no route to blame.
    void noteButton(bool down);

    // The game ran a frame of its own.
    void normalFrame(GJBaseGameLayer* layer);

    // The game's update was swallowed because the level is being held still.
    void frozenFrame();

    // One physics step is about to happen: this is where a route's button state goes in.
    void beforePhysicsStep();

    // Whether the player's own input should be kept out of the level for now.
    bool holdingTheControls() const { return busy() && m_phase != Phase::Recording; }

    void onLevelReset();

    // The game killed the run, or finished the level. Called from the game's own
    // handlers, so these only write down what happened: the work waits for a frame.
    void noteGameDeath(float x, float y, int objectId);
    void noteGameComplete();

    // Whether the player's own input should stay out of the level for now.
    bool suppressingGameplay() const;

    std::string headline() const;
    std::string detail() const;
    double progress() const;

    std::vector<std::string> const& lastFiles() const { return m_files; }

    // What the overlay draws: the level as the simulator was given it, the route it
    // planned, the path the real game actually took, and where each of them ended.
    Level const* capturedLevel() const { return m_level.get(); }
    std::vector<std::pair<float, float>> const& plannedPath() const { return m_plannedPath; }
    std::vector<std::pair<float, float>> const& realPath() const { return m_realPath; }
    std::pair<float, float> plannedEnd() const { return m_plannedEnd; }
    std::pair<float, float> realEnd() const { return m_realEnd; }
    int drawVersion() const { return m_drawVersion; }

private:
    Generator() = default;

    void askForReset(Phase after);
    void captureNow();
    void launchSearch();
    void collectSearch();
    void joinWorker();

    void runRecordFrame();
    void finishRecording(std::string why);
    void runReplayFrame();
    bool checkReplayEnded();
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
    int m_waited = 0;

    std::vector<char> m_inputs;
    int m_replayAt = 0;
    int m_lastFedIndex = -1;
    bool m_replayVerifying = false;

    // The step of the run a route starts from. Every replay begins feeding the route on
    // exactly this step, whichever frame the game happens to reach it on.
    int m_startOffset = 0;

    // The same moment on the game's own clock, which is what a macro is written in: a
    // bot playing it back counts every physics step from the level's start, including
    // the ones our step clock skips because nothing moved yet.
    int m_startFrame = 0;

    // Whether running the game beyond its own frames actually advances it. If it does
    // not, a check simply happens at normal speed instead of failing.
    bool m_fastWorks = true;

    // What the person at the keyboard did, on the game's own clock.
    std::vector<char> m_recorded;
    bool m_recordHeld = false;
    bool m_fromRecording = false;
    int m_recordFrom = 0;

    // The last stretch of the real replay, so a death can make the approach to it
    // expensive rather than only the spot itself.
    std::vector<std::pair<float, float>> m_realPath;

    // The route as the simulator flew it, for drawing over the real level.
    std::vector<std::pair<float, float>> m_plannedPath;
    std::pair<float, float> m_plannedEnd{0.f, 0.f};
    std::pair<float, float> m_realEnd{0.f, 0.f};
    int m_drawVersion = 0;

    // Set by the game's own death and completion handlers; acted on next frame, since
    // resetting or searching from inside them is asking for trouble.
    bool m_runOver = false;
    bool m_runDied = false;

    bool m_selfReset = false;
    Phase m_afterReset = Phase::Idle;
    bool m_finishedInGame = false;
    double m_diedAt = 0.0;

    std::vector<std::string> m_files;
    std::string m_note;
};

} // namespace mm
