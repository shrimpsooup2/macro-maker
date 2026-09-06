#pragma once

#include <cstdint>

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

    double stepDelta() const { return 1.0 / 240.0; }

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

    // A level that has just been reset is still sitting on the starting line: the game
    // has not flagged it as started, and until it does the player will not move however
    // many steps are fed to it. Both the capture and every replay begin from the step
    // after this returns, so they all count frames from the same place.
    int warmUpUntilMoving(int maxSteps);

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
};

} // namespace mm
