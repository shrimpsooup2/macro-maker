#include "gen/Generator.hpp"
#include "ui/Actions.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

constexpr char const* kButtonId = "macro-maker-button"_spr;
constexpr char const* kFallbackMenuId = "macro-maker-menu"_spr;
constexpr float kFallbackMenuOffset = 60.f;
constexpr float kButtonScale = 0.55f;

CCMenu* resolveHostMenu(PauseLayer* layer) {
    if (auto* column = layer->getChildByID("right-button-menu")) {
        if (auto* menu = typeinfo_cast<CCMenu*>(column)) return menu;
    }

    auto* fallback = CCMenu::create();
    fallback->setID(kFallbackMenuId);
    fallback->setPosition(CCDirector::sharedDirector()->getWinSize() -
                          ccp(kFallbackMenuOffset, kFallbackMenuOffset));
    layer->addChild(fallback);
    return fallback;
}

} // namespace

struct MacroPauseLayer : geode::Modify<MacroPauseLayer, PauseLayer> {
    void customSetup() {
        PauseLayer::customSetup();

        if (this->getChildByIDRecursive(kButtonId)) return;

        auto* host = resolveHostMenu(this);
        if (!host) return;

        auto* sprite = ButtonSprite::create(mm::pauseButtonLabel().c_str(), "bigFont.fnt",
                                            "GJ_button_01.png", kButtonScale);
        if (!sprite) return;

        auto* button =
            CCMenuItemSpriteExtra::create(sprite, this, menu_selector(MacroPauseLayer::onMacro));
        if (!button) return;

        button->setID(kButtonId);
        host->addChild(button);
        host->updateLayout();
    }

    // One button that does whatever makes sense next, then hands the level back so there
    // are frames to work in.
    void onMacro(CCObject*) {
        auto& generator = mm::Generator::get();

        if (generator.busy()) {
            mm::requestStop();
            return;
        }

        if (generator.phase() == mm::Phase::Finished) {
            mm::requestPlay();
        } else {
            mm::requestMake();
        }

        this->onResume(nullptr);
    }
};
