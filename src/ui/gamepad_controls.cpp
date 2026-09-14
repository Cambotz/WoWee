#include "ui/gamepad_controls.hpp"

#include "core/gamepad.hpp"
#include "core/input.hpp"
#include "core/logger.hpp"
#include "rendering/camera_controller.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

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
    if (escapeDown_) {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        escapeDown_ = false;
    }
    steering_ = false;
    zoomRemainder_ = 0.0f;
}

void GamepadControls::applyMovement(float x, float y) {
    // SDL's Y is positive downwards, and pushing the stick up means forward.
    const bool forward = pushed(-y, kWalkThreshold, heldKeys_[SDL_SCANCODE_W]);
    const bool back = pushed(y, kWalkThreshold, heldKeys_[SDL_SCANCODE_S]);
    // Q and E rather than A and D, as the on-screen stick does it: with no
    // right mouse button held this client turns the character on A and D and
    // strafes on Q and E, and a stick pushed sideways should sidestep rather
    // than swing the view - the right stick is what swings the view.
    const bool left = pushed(-x, kStrafeThreshold, heldKeys_[SDL_SCANCODE_Q]);
    const bool right = pushed(x, kStrafeThreshold, heldKeys_[SDL_SCANCODE_E]);

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

void GamepadControls::applyButtons() {
    const auto& pad = core::gamepad();
    std::size_t count = 0;
    const PadBinding* bindings = padBindings(count);
    for (std::size_t i = 0; i < count; ++i) {
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
    const bool live = enabled_ && pad.isConnected() && inWorld_;

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
                    "hold LB for 7-12, RB targets, L3 autoruns");
    }

    // Typing takes precedence over everything. The chat box is reached with a
    // keyboard, and a stick nudged while typing should not walk the character
    // out of town.
    if (io.WantTextInput) {
        reset();
        return;
    }

    applyMovement(pad.leftStick().x, pad.leftStick().y);
    applyLook(pad.rightStick().x, pad.rightStick().y, deltaTime);
    applyZoom(pad.rightTrigger(), pad.leftTrigger(), deltaTime);
    applyButtons();
}

}  // namespace ui
}  // namespace wowee
