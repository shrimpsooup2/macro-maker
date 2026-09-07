#pragma once

#include <cstdint>
#include <string>

class PlayLayer;
class GJBaseGameLayer;

namespace mm {

// A thin controller over the running level: freeze it, read the player, put it back to
// the start, and count the physics steps it has taken.
//
// What it deliberately does NOT do is decide how long a physics step is. Pinning the
// delta to a step's length was tried and the game would not have it: eight updates of
// exactly 1/240 bought one step between them, and the run died inside the settling
// frames it had been made to skip. The game already runs its physics at a fixed rate on
// its own frames, so it is left to do that, and the mod times everything off the steps
// it counts rather than off the clock it hands the game.
class Engine {
public:
    static Engine& get();

    void attach(PlayLayer* layer);
    void detach();

    bool valid() const { return m_layer != nullptr; }
    PlayLayer* layer() const { return m_layer; }
    bool owns(GJBaseGameLayer* layer) const;

    // While the level is frozen its update is dropped on the floor: nothing moves, which
    // is what the search wants while it thinks.
    void freeze();
    void unfreeze();
    bool frozen() const { return m_frozen; }
    bool shouldSwallowUpdate(GJBaseGameLayer* layer) const;
    bool inNestedUpdate() const { return m_nested; }

    // A click is a jump press on player one. Repeating the same value costs nothing.
    void setHeld(bool held);
    bool held() const { return m_held; }

    // True only inside our own call into the game's input, so a person leaning on the
    // key while a route plays can be told apart from the route itself.
    bool feedingInput() const { return m_feeding; }

    // Run the game a frame's worth beyond the one it is drawing, for replaying a route
    // faster than real time. Returns how many physics steps that bought, or -1 if the
    // level is not in a state to be run at all.
    int extraUpdate(double dt);

    // The run's own clock: physics steps in which the player actually travelled,
    // counted from the last reset.
    //
    // It has to be counted rather than worked out. The game calls PlayerObject::update
    // more often than it steps its physics -- a third of the steps in the calibration
    // recordings repeat the previous position exactly -- so a count of calls is not a
    // count of steps, while a count of the calls that moved the player is. Capture and
    // every replay anchor on this number, which is what keeps a macro landing on the
    // steps it was found on.
    void notePlayerStep(float x);
    void resetStepClock();
    int movingSteps() const { return m_movingSteps; }

    void resetLevel();
    void ensureLevelStarted();
    bool levelStarted() const;

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
    bool sawComplete() const { return m_completed; }
    void clearEvents() {
        m_died = false;
        m_completed = false;
    }

private:
    PlayLayer* m_layer = nullptr;
    bool m_frozen = false;
    bool m_nested = false;
    bool m_held = false;
    bool m_feeding = false;
    bool m_died = false;
    bool m_completed = false;

    int m_movingSteps = 0;
    float m_lastPlayerX = -1e9f;
};

} // namespace mm
