#include "Hud.hpp"

#include "gen/Generator.hpp"
#include "settings/Settings.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace mm {

namespace {

constexpr char const* kHudId = "macro-hud"_spr;
constexpr int kHudZOrder = 1500;

constexpr float kBarWidth = 190.f;
constexpr float kBarHeight = 5.f;
constexpr float kHeadlineScale = 0.42f;
constexpr float kDetailScale = 0.32f;
constexpr float kRowGap = 13.f;

constexpr ccColor4B kTrackColour{0, 0, 0, 110};

ccColor3B colourFor(Phase phase) {
    switch (phase) {
        case Phase::Searching:
            return ccColor3B{120, 200, 255};
        case Phase::Replaying:
        case Phase::ReplayReset:
            return ccColor3B{255, 220, 110};
        case Phase::Finished:
            return ccColor3B{130, 255, 160};
        case Phase::Failed:
            return ccColor3B{255, 130, 140};
        default:
            return ccColor3B{200, 205, 215};
    }
}

} // namespace

Hud* Hud::attachTo(PlayLayer* layer) {
    if (!layer) return nullptr;
    if (auto* existing = layer->getChildByID(kHudId)) {
        return typeinfo_cast<Hud*>(existing);
    }

    auto* hud = new (std::nothrow) Hud();
    if (!hud) return nullptr;
    if (!hud->init()) {
        delete hud;
        return nullptr;
    }
    hud->autorelease();
    hud->setID(kHudId);
    layer->addChild(hud, kHudZOrder);
    return hud;
}

bool Hud::init() {
    if (!CCNode::init()) return false;

    m_barTrack = CCLayerColor::create(kTrackColour, kBarWidth, kBarHeight);
    if (m_barTrack) {
        m_barTrack->ignoreAnchorPointForPosition(false);
        m_barTrack->setAnchorPoint(ccp(0.5f, 0.5f));
        this->addChild(m_barTrack);
    }

    m_barFill = CCLayerColor::create(ccc4(120, 200, 255, 220), kBarWidth, kBarHeight);
    if (m_barFill) {
        m_barFill->ignoreAnchorPointForPosition(false);
        m_barFill->setAnchorPoint(ccp(0.f, 0.5f));
        m_barFill->setScaleX(0.f);
        this->addChild(m_barFill);
    }

    m_headline = CCLabelBMFont::create("Macro Maker", "bigFont.fnt");
    if (m_headline) {
        m_headline->setScale(kHeadlineScale);
        this->addChild(m_headline);
    }

    m_detail = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_detail) {
        m_detail->setScale(kDetailScale);
        m_detail->setOpacity(190);
        this->addChild(m_detail);
    }

    layoutForScreen();
    this->scheduleUpdate();
    return true;
}

void Hud::layoutForScreen() {
    CCSize screen = CCDirector::sharedDirector()->getWinSize();
    Snapshot const& config = settings();

    this->setPosition(ccp(screen.width * 0.5f, screen.height * static_cast<float>(config.hudY)));
    this->setScale(static_cast<float>(config.hudScale));

    if (m_headline) m_headline->setPosition(ccp(0.f, kRowGap));
    if (m_detail) m_detail->setPosition(ccp(0.f, 0.f));
    if (m_barTrack) m_barTrack->setPosition(ccp(0.f, -kRowGap));
    if (m_barFill) m_barFill->setPosition(ccp(-kBarWidth * 0.5f, -kRowGap));
}

void Hud::update(float) {
    Generator const& generator = Generator::get();
    bool wanted = settings().showHud && generator.phase() != Phase::Idle;
    this->setVisible(wanted);
    if (!wanted) return;

    layoutForScreen();

    ccColor3B colour = colourFor(generator.phase());

    if (m_headline) {
        m_headline->setString(generator.headline().c_str());
        m_headline->setColor(colour);
    }
    if (m_detail) m_detail->setString(generator.detail().c_str());
    if (m_barFill) {
        m_barFill->setScaleX(static_cast<float>(std::clamp(generator.progress(), 0.0, 1.0)));
        m_barFill->setColor(colour);
    }
}

} // namespace mm
