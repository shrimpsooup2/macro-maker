#pragma once

#include <cocos2d.h>

class PlayLayer;

namespace mm {

// The readout in the corner: what the job is doing, how far the best route gets, and
// what happened to it.
class Hud : public cocos2d::CCNode {
public:
    static Hud* attachTo(PlayLayer* layer);

    bool init() override;
    void update(float dt) override;

private:
    void layoutForScreen();

    cocos2d::CCLabelBMFont* m_headline = nullptr;
    cocos2d::CCLabelBMFont* m_detail = nullptr;
    cocos2d::CCLayerColor* m_barTrack = nullptr;
    cocos2d::CCLayerColor* m_barFill = nullptr;
};

} // namespace mm
