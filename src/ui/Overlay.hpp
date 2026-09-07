#pragma once

#include <cocos2d.h>
#include <vector>

class PlayLayer;

namespace mm {

// Whether the picture is being shown. A flag rather than a setting on purpose: writing a
// setting from a hotkey re-enters the settings system from inside its own change
// callback, and that hung the game hard enough that Escape stopped working. The setting
// is still what it starts as; this is what a keypress changes.
bool overlayVisible();
void setOverlayVisible(bool visible);

// The level as the simulator sees it, drawn on top of the level as the game draws it.
//
// Every disagreement between the two is a bug, and every bug we have chased in this mod
// has been one: a box in the wrong place, a hazard that is not there, a route that walks
// through a wall. None of that is visible in a log and all of it is obvious in a
// picture, so here is the picture. It is drawn in the level's own coordinates, inside
// the layer the objects live in, so it moves with the camera by itself.
class Overlay : public cocos2d::CCNode {
public:
    static Overlay* attachTo(PlayLayer* layer);
    static Overlay* get();

    bool init() override;
    void update(float dt) override;

private:
    void redraw();
    void drawLive();

    // Two of them: the level and the route only change when the job does, and the
    // player's own boxes have to keep up with the player or they are worse than useless.
    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCDrawNode* m_live = nullptr;
    int m_shownVersion = -1;
    float m_shownAround = -1e9f;
};

} // namespace mm
