#pragma once

/**
 * gamepad_controls.hpp - what a controller's sticks and buttons mean.
 *
 * The device itself is core/gamepad.hpp. This is the half that decides a push
 * of the left stick is W held down and a press of A is the space bar, and then
 * says so through the two channels the client already listens on:
 *
 *   core::Input's virtual keys, for everything polled by scancode - movement,
 *   jumping, the action bar, targeting, autorun. The on-screen stick a phone
 *   draws has used them since it was written, which is the proof they carry a
 *   press the whole way: through the movement chain, the animations and the
 *   packets that announce it, with nothing in between knowing where the press
 *   came from.
 *
 *   ImGui's key queue, for the few the interface reads rather than the game -
 *   Escape above all, which goes through KeybindingManager and so through
 *   ImGui::IsKeyPressed. A virtual scancode never reaches it.
 *
 * Nothing here is platform-specific: SDL resolves a pad on Windows, macOS,
 * Linux and Android to the same Xbox-shaped button set, so the table below is
 * the table everywhere.
 *
 * Looting a corpse, talking to an NPC and buying from a vendor are clicks
 * rather than keys, so the pad has a pointer as well: Back switches the right
 * stick from turning the view to moving the mouse. The mouse it moves is the
 * real one - warped, so that everything which asks where the pointer is gets
 * the truth, from ImGui down to the client's own picking - and only the button
 * press is synthesised.
 */

#include <SDL2/SDL.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace wowee {
namespace rendering { class CameraController; }
namespace ui {

/// One button, and the key the client already answers on.
///
/// The mapping is a table rather than a chain of ifs because it is data: the
/// settings panel can show it, a later commit can let it be rebound, and a
/// reader can see the whole scheme at once.
struct PadBinding {
    SDL_GameControllerButton button;
    SDL_Scancode key;      ///< what core::Input is told is held
    const char* what;      ///< what a person reads on the settings panel
};

/// The default scheme, in the order it should be read.
///
/// Chosen against what this client actually answers to - the keys were taken
/// from the poll sites rather than from a list of what WoW binds - and against
/// what a pad has room for. Twelve action slots do not fit on a controller, so
/// six sit on the face and the pad, and the left bumper is held to reach the
/// other six: the client already reads shift as "the bottom-left bar", so the
/// modifier costs nothing and behaves exactly as it does on a keyboard.
[[nodiscard]] const PadBinding* padBindings(std::size_t& count);

/**
 * The controller, applied to the client, once a frame.
 *
 * Owns the keys it sets. A key this has not held down is never cleared by it,
 * so on a phone with a pad attached the on-screen stick and the pad can both
 * drive W without one of them switching the other off.
 */
class GamepadControls {
public:
    /// The sticks only steer in the world. On the login and character screens
    /// the pad is left to ImGui, which navigates its windows with it.
    void setInWorld(bool inWorld);
    [[nodiscard]] bool isInWorld() const { return inWorld_; }

    /// The camera the right stick turns and the triggers zoom. Without one
    /// the sticks still move the character.
    void setCameraController(rendering::CameraController* camera) { camera_ = camera; }

    /// The window the pointer is moved inside. Without one there is no
    /// pointer mode, because there is nowhere to put the cursor.
    void setWindow(SDL_Window* window) { window_ = window; }

    /// How far the pointer travels this frame for a given push of the stick.
    ///
    /// Squared, so the one stick does both jobs a pointer needs: a nudge
    /// creeps across a button at a few pixels a second, a full push crosses
    /// the window in under two. Linear, the speed that lands on a 20 pixel
    /// button is too slow to cross a screen and the one that crosses a screen
    /// cannot land on the button.
    ///
    /// Static and taking everything it uses, so the curve can be checked
    /// without a window, a pad or a frame.
    [[nodiscard]] static glm::vec2 pointerStep(float stickX, float stickY, float deltaTime) {
        const float magnitude = std::sqrt(stickX * stickX + stickY * stickY);
        if (magnitude <= 0.0f || deltaTime <= 0.0f) return glm::vec2(0.0f);
        const float clamped = std::min(magnitude, 1.0f);
        const float speed = clamped * clamped * kPointerPointsPerSecond;
        return glm::vec2(stickX / magnitude, stickY / magnitude) * (speed * deltaTime);
    }

    /// Reads the pad and applies it. Call once a frame, before Input::update,
    /// so that a button pressed this frame reads as just-pressed this frame
    /// rather than next.
    void update(float deltaTime);

    /// Releases every key this is holding. On leaving the world, on losing the
    /// pad, and when the interface takes the keyboard.
    void reset();

    /// True while the stick is being pushed, which is when the character
    /// should face where the camera looks - the same thing the right mouse
    /// button means on a desktop and a dragging finger means on a phone.
    [[nodiscard]] bool isSteering() const { return steering_; }

    /// Whether a connected pad is allowed to drive anything.
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const { return enabled_; }

    /// How fast the right stick turns the view, in degrees a second at full
    /// deflection. Independent of the mouse's own sensitivity: the two are
    /// different instruments and a player who slows one has said nothing
    /// about the other.
    void setLookDegreesPerSecond(float degrees);
    [[nodiscard]] float lookDegreesPerSecond() const { return lookDegreesPerSecond_; }

    /// Pushing the stick up looks up, unless this is set.
    void setInvertLook(bool invert) { invertLook_ = invert; }
    [[nodiscard]] bool isInvertLook() const { return invertLook_; }

private:
    /// Sets a virtual key and remembers that this is the thing holding it.
    void holdKey(SDL_Scancode key, bool held);

    /// The left stick, as the four keys the client walks on.
    void applyMovement(float x, float y);
    /// The right stick, as a turn of the view.
    void applyLook(float x, float y, float deltaTime);
    /// The triggers, as notches of the wheel.
    void applyZoom(float in, float out, float deltaTime);
    /// The buttons, as the keys in the table.
    void applyButtons();
    /// The right stick, as the mouse pointer, and two buttons as its clicks.
    void applyPointer(float deltaTime);
    /// Holds a mouse button and remembers that this is what is holding it.
    void holdMouseButton(int button, bool held);
    /// Turns the pointer on or off, putting the cursor somewhere sensible.
    void setPointerMode(bool on);

    rendering::CameraController* camera_ = nullptr;
    SDL_Window* window_ = nullptr;
    bool inWorld_ = false;
    /// The right stick moves the pointer instead of the view.
    bool pointerMode_ = false;
    /// Back is read here rather than through the table, because it toggles
    /// rather than holds and the edge is the whole of what it means.
    bool pointerToggleWasDown_ = false;
    /// Where the pointer is, in window coordinates. Kept as floats because a
    /// stick pushed gently is worth a fraction of a pixel a frame, and an int
    /// would round every one of them to nothing.
    float pointerX_ = 0.0f;
    float pointerY_ = 0.0f;
    /// Which mouse buttons this is holding, so letting go clears those and
    /// only those. Index is SDL's, which is one-based.
    std::array<bool, 8> heldMouseButtons_{};
    bool enabled_ = true;
    bool steering_ = false;
    bool invertLook_ = false;
    bool announced_ = false;

    /// Which keys this is currently holding down, so that letting go clears
    /// those and only those.
    std::array<bool, SDL_NUM_SCANCODES> heldKeys_{};

    /// Whether the interface's Escape is currently down, so the press and the
    /// release are each sent once. ImGui counts repeats itself.
    bool escapeDown_ = false;

    float lookDegreesPerSecond_ = 180.0f;
    /// Left over from a zoom that did not reach a whole notch, so a trigger
    /// held lightly still zooms rather than rounding to nothing every frame.
    float zoomRemainder_ = 0.0f;

    /// Held before the direction lets go, so a thumb resting near the edge of
    /// the deadzone does not stutter between walking and standing. The
    /// numbers are the on-screen stick's, which were tuned against a thumb on
    /// a real control and behave the same way here.
    static constexpr float kWalkThreshold = 0.30f;
    static constexpr float kStrafeThreshold = 0.45f;
    static constexpr float kReleaseHysteresis = 0.10f;
    /// Notches of the mouse wheel a fully pulled trigger is worth per second.
    static constexpr float kZoomNotchesPerSecond = 6.0f;
    /// How fast the pointer crosses the window at full deflection, in window
    /// points a second. Fast enough to cross a 1280-wide window in a second
    /// and a half, which is about as slow as a pointer can be before it feels
    /// broken.
    static constexpr float kPointerPointsPerSecond = 900.0f;
};

/// The one instance, reached the way the touch controls are.
GamepadControls& gamepadControls();

}  // namespace ui
}  // namespace wowee
