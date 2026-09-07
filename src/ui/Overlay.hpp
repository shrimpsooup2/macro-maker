#pragma once

#include <cocos2d.h>
#include <vector>

class PlayLayer;

namespace mm {

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

    cocos2d::CCDrawNode* m_draw = nullptr;
    int m_shownVersion = -1;
    float m_shownAround = -1e9f;
};

} // namespace mm
