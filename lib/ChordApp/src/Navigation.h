#pragma once

#include "Analysis.h"
#include "UiLayout.h"
#include "ModeOrder.h"
#include <cstdlib>

namespace chord::ui {

enum class Swipe { None, Left, Right, Up, Down };
enum class Layer { Playing, KeyPicker, Settings };
enum class GestureAction { None, NextView, PreviousView, OpenKeyPicker, OpenSettings, ClosePanel, NextScale, PreviousScale };

inline Swipe swipeAt(int startX, int startY, int endX, int endY) {
    // Begin in the display content, not the footer or Core2's button strip.
    if (startX < 0 || startX >= 320 || startY < 0 || startY >= footerY ||
        endX < 0 || endX >= 320 || endY < 0 || endY >= 280) return Swipe::None;
    const int dx = endX - startX, dy = endY - startY;
    const int horizontal = std::abs(dx), vertical = std::abs(dy);
    if (horizontal >= 44 && horizontal >= vertical * 2) return dx < 0 ? Swipe::Left : Swipe::Right;
    if (vertical >= 44 && vertical >= horizontal * 2) return dy < 0 ? Swipe::Up : Swipe::Down;
    return Swipe::None;
}

inline GestureAction gestureAction(Swipe swipe, Layer layer) {
    if (layer == Layer::KeyPicker) {
        if (swipe == Swipe::Left) return GestureAction::NextScale;
        if (swipe == Swipe::Right) return GestureAction::PreviousScale;
        if (swipe == Swipe::Down) return GestureAction::ClosePanel;
    } else if (layer == Layer::Settings) {
        if (swipe == Swipe::Down) return GestureAction::ClosePanel;
    } else {
        if (swipe == Swipe::Left) return GestureAction::NextView;
        if (swipe == Swipe::Right) return GestureAction::PreviousView;
        if (swipe == Swipe::Up) return GestureAction::OpenKeyPicker;
        if (swipe == Swipe::Down) return GestureAction::OpenSettings;
    }
    return GestureAction::None;
}

inline int viewIndex(Mode mode) {
    return mode == Mode::Harmony ? 1 : mode == Mode::Notation ? 2 : 0;
}
inline int steppedView(Mode mode, int direction) { return (viewIndex(mode) + (direction < 0 ? 2 : 1)) % 3; }
inline ScaleMode steppedScale(ScaleMode scale, int direction) {
    const int count = int(modesByBrightness.size());
    return modesByBrightness[(brightnessIndex(scale) + (direction < 0 ? count - 1 : 1)) % count];
}

} // namespace chord::ui
