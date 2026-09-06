#include "Actions.hpp"

#include "gen/Generator.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace mm {

namespace {

constexpr char const* kToastId = "macro-toast"_spr;
constexpr int kToastZOrder = 2000;
constexpr float kToastScale = 0.45f;
constexpr float kToastFade = 0.3f;
constexpr float kToastHold = 1.6f;
constexpr float kToastMargin = 26.f;

constexpr ccColor3B kGood{130, 235, 160};
constexpr ccColor3B kBusy{120, 200, 255};
constexpr ccColor3B kProblem{255, 135, 145};

} // namespace

void showToast(std::string const& text, ccColor3B colour) {
    auto* director = CCDirector::sharedDirector();
    if (!director) return;

    auto* scene = director->getRunningScene();
    if (!scene) return;

    if (auto* old = scene->getChildByID(kToastId)) old->removeFromParent();

    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    if (!label) return;

    CCSize screen = director->getWinSize();
    label->setID(kToastId);
    label->setScale(kToastScale);
    label->setColor(colour);
    label->setOpacity(0);
    label->setPosition(ccp(screen.width * 0.5f, kToastMargin));
    scene->addChild(label, kToastZOrder);

    label->runAction(CCSequence::create(CCFadeIn::create(kToastFade),
                                        CCDelayTime::create(kToastHold),
                                        CCFadeOut::create(kToastFade), nullptr));
}

void requestMake() {
    if (!PlayLayer::get()) {
        showToast("Open a level first", kProblem);
        return;
    }

    Generator& generator = Generator::get();
    if (generator.busy()) {
        showToast("Already working", kBusy);
        return;
    }

    if (generator.startMaking()) {
        showToast("Making a macro for this level", kBusy);
    } else {
        showToast("Could not start", kProblem);
    }
}

void requestPlay() {
    if (!PlayLayer::get()) {
        showToast("Open a level first", kProblem);
        return;
    }

    Generator& generator = Generator::get();
    if (generator.busy()) {
        showToast("Still working", kBusy);
        return;
    }

    if (generator.startPlayback()) {
        showToast("Playing the macro", kGood);
    } else {
        showToast("No macro for this level yet", kProblem);
    }
}

void requestStop() {
    Generator& generator = Generator::get();
    if (!generator.busy()) {
        showToast("Nothing is running", kBusy);
        return;
    }
    generator.stop();
    showToast("Stopped", kBusy);
}

std::string pauseButtonLabel() {
    Generator const& generator = Generator::get();
    switch (generator.phase()) {
        case Phase::Resetting:
        case Phase::Capturing:
        case Phase::Searching:
        case Phase::ReplayReset:
        case Phase::Replaying:
            return "Stop";
        case Phase::Finished:
            return "Replay";
        case Phase::Failed:
            return "Retry";
        case Phase::Idle:
        default:
            return "Macro";
    }
}

} // namespace mm
