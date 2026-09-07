#pragma once

#include <cocos2d.h>
#include <string>

namespace mm {

// The three things a person can ask for, shared by the hotkeys and the pause menu button
// so both behave identically.
void requestMake();
void requestPlay();
void requestRecord();
void toggleOverlay();
void requestStop();

// What the pause menu button should say right now.
std::string pauseButtonLabel();

void showToast(std::string const& text, cocos2d::ccColor3B colour);

} // namespace mm
