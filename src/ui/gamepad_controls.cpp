#include "ui/gamepad_controls.hpp"

#include "core/gamepad.hpp"
#include "core/input.hpp"
#include "core/logger.hpp"
#include "rendering/camera_controller.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace wowee {
namespace ui {

namespace {

/// Whether a direction is pushed far enough to count, given whether it was
/// already. The release threshold is lower than the press one, so a thumb
/// resting on the edge does not flicker between walking and standing.
bool pushed(float axis, float threshold, bool wasOn) {
    const float release = std::max(threshold - 0.10f, 0.05f);
    return axis > (wasOn ? release : threshold);
}

}  // namespace

GamepadControls& gamepadControls() {
    static GamepadControls instance;
    return instance;
}

void GamepadControls::setInWorld(bool inWorld) {
    if (inWorld_ == inWorld) return;
    inWorld_ = inWorld;
    if (!inWorld_) reset();
}

void GamepadControls::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled_) reset();
}

void GamepadControls::setLookDegreesPerSecond(float degrees) {
    lookDegreesPerSecond_ = std::clamp(degrees, 30.0f, 720.0f);
}

void GamepadControls::holdKey(SDL_Scancode key, bool held) {
    if (key <= SDL_SCANCODE_UNKNOWN || key >= SDL_NUM_SCANCODES) return;
    const auto i = static_cast<std::size_t>(key);
    // A key this is not holding is left alone. Two things can drive the same
    // virtual key - a phone's on-screen stick is the other - and whichever of
    // them is idle must not switch the other one off.
    if (!held && !heldKeys_[i]) return;
    heldKeys_[i] = held;
    core::Input::getInstance().setVirtualKey(key, held);
}

void GamepadControls::reset() {
    for (std::size_t i = 0; i < heldKeys_.size(); ++i) {
        if (!heldKeys_[i]) continue;
        heldKeys_[i] = false;
        core::Input::getInstance().setVirtualKey(static_cast<SDL_Scancode>(i), false);
    }
    for (std::size_t i = 0; i < heldMouseButtons_.size(); ++i) {
        if (!heldMouseButtons_[i]) continue;
        heldMouseButtons_[i] = false;
        const int button = static_cast<int>(i);
        core::Input::getInstance().setVirtualMouseButton(button, false);
        ImGui::GetIO().AddMouseButtonEvent(button == SDL_BUTTON_RIGHT ? 1 : 0, false);
    }
    if (escapeDown_) {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        escapeDown_ = false;
    }
    steering_ = false;
    zoomRemainder_ = 0.0f;
    touchTrail_.reset();
    touchClickButton_ = 0;
}

void GamepadControls::applyMovement(float x, float y) {
    // SDL's Y is positive downwards, and pushing the stick up means forward.
    const bool forward = pushed(-y, kWalkThreshold, heldKeys_[static_cast<std::size_t>(SDL_SCANCODE_W)]);
    const bool back = pushed(y, kWalkThreshold, heldKeys_[static_cast<std::size_t>(SDL_SCANCODE_S)]);
    // Q and E rather than A and D, as the on-screen stick does it: with no
    // right mouse button held this client turns the character on A and D and
    // strafes on Q and E, and a stick pushed sideways should sidestep rather
    // than swing the view - the right stick is what swings the view.
    const bool left = pushed(-x, kStrafeThreshold, heldKeys_[static_cast<std::size_t>(SDL_SCANCODE_Q)]);
    const bool right = pushed(x, kStrafeThreshold, heldKeys_[static_cast<std::size_t>(SDL_SCANCODE_E)]);

    holdKey(SDL_SCANCODE_W, forward);
    holdKey(SDL_SCANCODE_S, back);
    holdKey(SDL_SCANCODE_Q, left);
    holdKey(SDL_SCANCODE_E, right);

    // Facing follows the camera while the stick is pushed, which is what the
    // right mouse button does on a desktop. Without it the character walks
    // sideways across the screen while still facing wherever they last were.
    steering_ = forward || back || left || right;
}

void GamepadControls::applyLook(float x, float y, float deltaTime) {
    if (!camera_) return;
    if (x == 0.0f && y == 0.0f) return;
    // applyLookDelta takes mouse pixels and multiplies by the mouse's own
    // sensitivity. Dividing it out here keeps the pad's turn rate in degrees
    // a second, so slowing the mouse does not slow the pad.
    const float mouseSensitivity = std::max(camera_->getMouseSensitivity(), 0.0001f);
    const float degrees = lookDegreesPerSecond_ * deltaTime;
    const float pixels = degrees / mouseSensitivity;
    const float up = invertLook_ ? -y : y;
    camera_->applyLookDelta(x * pixels, up * pixels);
}

void GamepadControls::applyZoom(float in, float out, float deltaTime) {
    if (!camera_) return;
    const float pull = in - out;
    if (pull == 0.0f) {
        zoomRemainder_ = 0.0f;
        return;
    }
    // Carried between frames. A trigger held a third of the way is worth two
    // notches a second, which is less than one in any single frame - rounded
    // away each time, it would be a trigger that does nothing.
    zoomRemainder_ += pull * kZoomNotchesPerSecond * deltaTime;
    const float whole = std::trunc(zoomRemainder_);
    if (whole == 0.0f) return;
    zoomRemainder_ -= whole;
    camera_->processMouseWheel(whole);
}

void GamepadControls::holdMouseButton(int button, bool held) {
    if (button < 0 || button >= static_cast<int>(heldMouseButtons_.size())) return;
    const auto i = static_cast<std::size_t>(button);
    // As with the keys: a button this is not holding is left alone, so a real
    // mouse and the pad cannot switch each other off.
    if (!held && !heldMouseButtons_[i]) return;
    if (held == heldMouseButtons_[i]) return;
    heldMouseButtons_[i] = held;
    // Both channels, because both are read. The world's own targeting polls
    // core::Input, and every panel asks ImGui.
    core::Input::getInstance().setVirtualMouseButton(button, held);
    ImGui::GetIO().AddMouseButtonEvent(button == SDL_BUTTON_RIGHT ? 1 : 0, held);
}

void GamepadControls::setPointerMode(bool on) {
    if (pointerMode_ == on) return;
    pointerMode_ = on;
    // Nothing is released here. Every path that turns the pointer off either
    // resets all held buttons or goes on to applyClicks, which stops counting
    // A and X - and a release here would drop a click the touchpad still holds.
    if (!on) return;
    // Starts where the pointer already is rather than at the middle of the
    // screen, so turning it on twice does not throw away where it was left.
    int x = 0;
    int y = 0;
    SDL_GetMouseState(&x, &y);
    pointerX_ = static_cast<float>(x);
    pointerY_ = static_cast<float>(y);
    if (window_ && (x == 0 && y == 0)) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        pointerX_ = static_cast<float>(w) * 0.5f;
        pointerY_ = static_cast<float>(h) * 0.5f;
    }
}

void GamepadControls::applyPointer(float deltaTime) {
    if (!window_) return;
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (w <= 0 || h <= 0) return;

    const auto& pad = core::gamepad();
    const glm::vec2 step = pointerStep(pad.rightStick().x, pad.rightStick().y, deltaTime);
    if (step.x != 0.0f || step.y != 0.0f) {
        pointerX_ = std::clamp(pointerX_ + step.x, 0.0f, static_cast<float>(w - 1));
        pointerY_ = std::clamp(pointerY_ + step.y, 0.0f, static_cast<float>(h - 1));
        SDL_WarpMouseInWindow(window_, static_cast<int>(pointerX_), static_cast<int>(pointerY_));
    }
}

void GamepadControls::applyTouchpad() {
    const auto& finger = core::gamepad().touch(0);
    // Followed every frame, before anything can return, so a finger that
    // was down while there was no window is not read as one long slide.
    const glm::vec2 slide = touchTrail_.follow(finger.down, finger.position);
    if (!window_ || (slide.x == 0.0f && slide.y == 0.0f)) return;

    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (w <= 0 || h <= 0) return;

    // Starts from where the cursor really is. The touchpad works outside
    // pointer mode too, so the stored position can be stale - left from the
    // last time the stick moved it, with a real mouse used since.
    int x = 0;
    int y = 0;
    SDL_GetMouseState(&x, &y);
    if (std::abs(x - static_cast<int>(pointerX_)) > 1 || std::abs(y - static_cast<int>(pointerY_)) > 1) {
        pointerX_ = static_cast<float>(x);
        pointerY_ = static_cast<float>(y);
    }
    const glm::vec2 step = touchStep(slide, static_cast<float>(w));
    pointerX_ = std::clamp(pointerX_ + step.x, 0.0f, static_cast<float>(w - 1));
    pointerY_ = std::clamp(pointerY_ + step.y, 0.0f, static_cast<float>(h - 1));
    SDL_WarpMouseInWindow(window_, static_cast<int>(pointerX_), static_cast<int>(pointerY_));
}

void GamepadControls::applyClicks() {
    const auto& pad = core::gamepad();
    if (!pad.held(SDL_CONTROLLER_BUTTON_TOUCHPAD)) {
        touchClickButton_ = 0;
    } else if (touchClickButton_ == 0) {
        touchClickButton_ = pad.touch(1).down ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
    }

    // A is the click, as it is on every console, and X is the right click -
    // which in this game opens a corpse, uses a door and brings up a unit's
    // menu, so a pad without one cannot loot. Those two only count while the
    // pointer is up. Both channels are set once, from every source together,
    // so one source letting go cannot release a button another still holds.
    const bool left = (pointerMode_ && pad.held(SDL_CONTROLLER_BUTTON_A)) ||
                      touchClickButton_ == SDL_BUTTON_LEFT;
    const bool right = (pointerMode_ && pad.held(SDL_CONTROLLER_BUTTON_X)) ||
                       touchClickButton_ == SDL_BUTTON_RIGHT;
    holdMouseButton(SDL_BUTTON_LEFT, left);
    holdMouseButton(SDL_BUTTON_RIGHT, right);
}

void GamepadControls::applyButtons() {
    const auto& pad = core::gamepad();
    std::size_t count = 0;
    const PadBinding* bindings = padBindings(count);
    for (std::size_t i = 0; i < count; ++i) {
        // A and X are the pointer's two clicks while it is up. Jumping and
        // casting from the same press would fire a spell at whatever was
        // under the cursor every time a window was clicked.
        if (pointerMode_ && (bindings[i].button == SDL_CONTROLLER_BUTTON_A ||
                             bindings[i].button == SDL_CONTROLLER_BUTTON_X)) {
            holdKey(bindings[i].key, false);
            continue;
        }
        holdKey(bindings[i].key, pad.held(bindings[i].button));
    }

    // Escape is the interface's, not the game's: it is read through
    // KeybindingManager, which asks ImGui. A virtual scancode never reaches
    // it, so this one goes on ImGui's own queue - once down, once up, because
    // ImGui counts the repeats itself.
    const bool escape = pad.held(SDL_CONTROLLER_BUTTON_B) ||
                        pad.held(SDL_CONTROLLER_BUTTON_START);
    if (escape != escapeDown_) {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, escape);
        escapeDown_ = escape;
    }
}

void GamepadControls::update(float deltaTime) {
    auto& pad = core::gamepad();
    ImGuiIO& io = ImGui::GetIO();
    // A pad is only read while the window has focus. SDL does not update a
    // controller for an unfocused window unless it is told to, so whatever
    // the stick last said stays said - and alt-tabbing mid-stride would leave
    // the character walking north for as long as the player was away.
    const bool focused =
        !window_ || (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool live = enabled_ && pad.isConnected() && inWorld_ && focused;

    // ImGui navigates its own windows with a pad, which is what the login and
    // character screens want and the opposite of what the world wants: in the
    // world the D-pad casts spells, and a panel left open would take those
    // presses as "move the highlight" as well. So the nav flag follows who is
    // driving.
    if (live) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }

    if (!pad.isConnected()) announced_ = false;
    if (!live) {
        // Leaving the world, losing the pad or switching it off ends pointer
        // mode; losing focus for a moment does not. Coming back from another
        // window to find the pointer gone would be a small betrayal of the
        // mode the player left it in.
        if (!enabled_ || !pad.isConnected() || !inWorld_) setPointerMode(false);
        reset();
        return;
    }

    if (!announced_) {
        announced_ = true;
        // Once per connection, at warning, because a player who has plugged a
        // pad in and is wondering whether the client saw it has exactly one
        // place to look, and that log is warnings only.
        LOG_WARNING("Gamepad: ", pad.describe(),
                    " - left stick moves, right stick looks, triggers zoom, "
                    "A jumps, B closes, X/Y and the D-pad are actions 1-6, "
                    "hold LB for 7-12, RB targets, L3 autoruns, Back gives you a pointer",
                    pad.hasTouchpad() ? ", and the touchpad is a trackpad - click it, or click "
                                        "with two fingers to right-click"
                                      : "");
    }

    // Typing takes precedence over everything. The chat box is reached with a
    // keyboard, and a stick nudged while typing should not walk the character
    // out of town.
    if (io.WantTextInput) {
        reset();
        return;
    }

    // Back switches the right stick between the view and the pointer. On its
    // edge rather than while held: it is a mode, and a mode that lasted only
    // as long as a thumb could hold a button would be no use for buying from
    // a vendor.
    const bool toggle = pad.held(SDL_CONTROLLER_BUTTON_BACK);
    if (toggle && !pointerToggleWasDown_) {
        setPointerMode(!pointerMode_);
        // At warning, because the two modes look identical apart from the
        // cursor, and a player wondering why the stick stopped turning the
        // view has one place to look.
        LOG_WARNING("Gamepad: the right stick now ",
                    pointerMode_ ? "moves the pointer" : "turns the view");
    }
    pointerToggleWasDown_ = toggle;

    // Walking and zooming work in both modes. A pointer that stopped the
    // player from stepping back out of a fire would be worse than none.
    applyMovement(pad.leftStick().x, pad.leftStick().y);
    applyZoom(pad.rightTrigger(), pad.leftTrigger(), deltaTime);
    if (pointerMode_) {
        applyPointer(deltaTime);
    } else {
        applyLook(pad.rightStick().x, pad.rightStick().y, deltaTime);
    }
    applyTouchpad();
    applyClicks();
    applyButtons();
}

}  // namespace ui
}  // namespace wowee
