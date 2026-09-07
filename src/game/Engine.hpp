#pragma once

#include <cstdint>
#include <string>

class PlayLayer;
class GJBaseGameLayer;

namespace mm {

enum class StepResult {
    Alive,
    Dead,
    Finished,
};

// A thin controller that turns the running level into something we can drive: step
// exactly one physics step, read the player, start the level, put it back to the
// beginning.
//
// Nothing about GD's movement is reimplemented here -- this is the real game running --
// which is what makes it worth checking a route against.
class Engine {
public:
    static Engine& get();

    void attach(PlayLayer* layer);
    void detach();

    bool valid() const { return m_layer != nullptr; }
    PlayLayer* layer() const { return m_layer; }
    bool owns(GJBaseGameLayer* layer) const;

    // While we are driving, the game's own update calls are dropped on the floor and the
    // level is stepped by hand instead, one physics step at a time.
    void beginDriving();
    void endDriving();
    bool driving() const { return m_driving; }

    bool shouldSwallowUpdate(GJBaseGameLayer* layer) const;
    bool shouldPinDelta(GJBaseGameLayer* layer) const;

    double stepDelta() const { return m_stepDelta; }

    // What one call to the game's update is worth, worked out by trying it.
    //
    // The game decides how many physics steps a frame is worth from the delta it is
    // handed, and the arithmetic it uses is not something to guess at: a delta of
    // exactly one step's length can land a hair under the boundary and buy nothing at
    // all, which looks from outside like a level that refuses to move. So the delta is
    // measured against the run's own step clock instead -- feed the game a length, count
    // the steps that came out, and keep the smallest length that reliably buys one.
    bool calibrateStep();
    bool calibrated() const { return m_calibrated; }
    int stepsPerUpdate() const { return m_stepsPerUpdate; }
    std::string calibrationNote() const { return m_calibration; }

    // A click is a jump press on player one. Repeating the same value costs nothing.
    void setHeld(bool held);
    bool held() const { return m_held; }

    // True only inside our own call into the game's input. While a route is being driven
    // everything else that reaches handleButton is a person leaning on the key, and
    // would put the run out of step with the macro being written.
    bool feedingInput() const { return m_feeding; }

    StepResult stepOnce();

    void ensureLevelStarted();
    bool levelStarted() const;

    // The run's own clock: physics steps in which the player actually travelled,
    // counted from the last reset.
    //
    // It has to be counted rather than worked out. The game calls PlayerObject::update
    // more often than it steps its physics -- a third of the steps in the calibration
    // recordings repeat the previous position exactly -- so a count of calls is not a
    // count of steps, while a count of the calls that moved the player is. Capture and
    // replay both anchor on this number, which is what keeps a macro landing on the
    // frames it was found on.
    void notePlayerStep(float x);
    void resetStepClock();
    int movingSteps() const { return m_movingSteps; }

    // Step, with the button up, until the clock reaches `target`. Returns the number of
    // steps taken, or -1 if the run died or the level stopped advancing.
    int driveToStep(int target, int maxSteps);

    void resetLevel();

    float playerX() const;
    float playerY() const;
    double playerVelocityY() const;
    bool playerOnGround() const;
    bool playerIsDead() const;
    bool playerUpsideDown() const;
    bool playerMini() const;
    int playerMode() const;             // one of mm::Mode
    int playerSpeed() const;            // 0..4, the speed portal index

    // Called from the hooks when the game tried to kill the player or finish the level.
    void notifyDeath() { m_died = true; }
    void notifyComplete() { m_completed = true; }
    bool sawDeath() const { return m_died; }
    void clearEvents() {
        m_died = false;
        m_completed = false;
    }

    std::uint64_t stepsTaken() const { return m_steps; }

private:
    PlayLayer* m_layer = nullptr;
    bool m_driving = false;
    bool m_inStep = false;
    bool m_held = false;
    bool m_feeding = false;
    bool m_died = false;
    bool m_completed = false;
    std::uint64_t m_steps = 0;

    int m_movingSteps = 0;
    float m_lastPlayerX = -1e9f;

    double m_stepDelta = 1.0 / 240.0;
    bool m_pinDelta = true;
    bool m_calibrated = false;
    int m_stepsPerUpdate = 1;
    std::string m_calibration;
};

} // namespace mm
