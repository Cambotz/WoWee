// The controller's stick arithmetic, and the scheme its buttons carry.
//
// The device half cannot be tested without a device. These are the parts that
// decide what a reading means, which is where a stick feels wrong or right:
// the deadzone, the rescale, and whether two buttons have been given the same
// job by accident.
#include <catch_amalgamated.hpp>

#include "core/gamepad.hpp"
#include "ui/gamepad_controls.hpp"

#include <cmath>
#include <set>
#include <string>

using wowee::core::axisFraction;
using wowee::core::stickVector;
using wowee::core::triggerFraction;

namespace {
float length(const glm::vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
}  // namespace

TEST_CASE("an axis reading is a fraction of full deflection") {
    CHECK(axisFraction(0) == Catch::Approx(0.0f));
    CHECK(axisFraction(32767) == Catch::Approx(1.0f));
    // SDL's range is asymmetric: -32768 is one step past the positive end.
    // Without the clamp this reads -1.00003, and everything downstream
    // multiplies by it.
    CHECK(axisFraction(-32768) == Catch::Approx(-1.0f));
    CHECK(axisFraction(16384) == Catch::Approx(0.5f).margin(0.001f));
}

TEST_CASE("a resting stick reads as centred") {
    CHECK(length(stickVector(0.0f, 0.0f, 0.2f)) == Catch::Approx(0.0f));
    CHECK(length(stickVector(0.15f, 0.0f, 0.2f)) == Catch::Approx(0.0f));
    // Inside the circle, not inside the square: 0.15 on each axis is 0.21
    // away from centre, which a per-axis deadzone would call rest.
    CHECK(length(stickVector(0.15f, 0.15f, 0.2f)) > 0.0f);
}

TEST_CASE("the deadzone is taken radially, not per axis") {
    // A stick pushed diagonally to its corner must not read longer than one,
    // or a character walks faster north-east than north.
    CHECK(length(stickVector(1.0f, 1.0f, 0.2f)) == Catch::Approx(1.0f));
    CHECK(length(stickVector(-1.0f, 1.0f, 0.2f)) == Catch::Approx(1.0f));
    // And the direction of the push is kept.
    const glm::vec2 diagonal = stickVector(0.7f, 0.7f, 0.2f);
    CHECK(diagonal.x == Catch::Approx(diagonal.y));
}

TEST_CASE("a straight push stays straight") {
    // A worn stick reads a little sideways when pushed forward. On a camera
    // that is a drift that never settles, so the cross axis has to come out
    // of the deadzone as zero.
    const glm::vec2 forward = stickVector(0.0f, -0.8f, 0.2f);
    CHECK(forward.x == Catch::Approx(0.0f));
    CHECK(forward.y < 0.0f);
}

TEST_CASE("what is left of the range starts at zero and ends at one") {
    // The first movement outside the deadzone must be the slowest one, not a
    // jump to a fifth of full speed.
    const float justOutside = length(stickVector(0.0f, 0.21f, 0.2f));
    CHECK(justOutside > 0.0f);
    CHECK(justOutside < 0.05f);
    CHECK(length(stickVector(0.0f, 1.0f, 0.2f)) == Catch::Approx(1.0f));
    // Half way along what is left is half speed.
    CHECK(length(stickVector(0.0f, 0.6f, 0.2f)) == Catch::Approx(0.5f).margin(0.001f));
}

TEST_CASE("a deadzone of zero leaves the reading alone") {
    CHECK(length(stickVector(0.0f, 0.3f, 0.0f)) == Catch::Approx(0.3f));
}

TEST_CASE("a trigger rests at nothing and pulls to one") {
    CHECK(triggerFraction(0.0f, 0.08f) == Catch::Approx(0.0f));
    CHECK(triggerFraction(0.05f, 0.08f) == Catch::Approx(0.0f));
    CHECK(triggerFraction(1.0f, 0.08f) == Catch::Approx(1.0f));
    CHECK(triggerFraction(0.54f, 0.08f) == Catch::Approx(0.5f).margin(0.001f));
    // A trigger cannot be pushed the other way, whatever the driver says.
    CHECK(triggerFraction(-0.4f, 0.08f) == Catch::Approx(0.0f));
}

TEST_CASE("no button is given two jobs and no job two buttons") {
    std::size_t count = 0;
    const wowee::ui::PadBinding* bindings = wowee::ui::padBindings(count);
    REQUIRE(count > 0);

    std::set<int> buttons;
    std::set<int> keys;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& binding = bindings[i];
        INFO(binding.what);
        // A button bound twice runs whichever line is later and looks like a
        // dead button; a key bound twice is two pad buttons that do the same
        // thing, which is a wasted button on a device that has ten.
        CHECK(buttons.insert(static_cast<int>(binding.button)).second);
        CHECK(keys.insert(static_cast<int>(binding.key)).second);
        CHECK(binding.button >= 0);
        CHECK(binding.button < SDL_CONTROLLER_BUTTON_MAX);
        CHECK(binding.key > SDL_SCANCODE_UNKNOWN);
        CHECK(binding.key < SDL_NUM_SCANCODES);
        // Every row is shown to a player, so every row needs a name.
        REQUIRE(binding.what != nullptr);
        CHECK(binding.what[0] != '\0');
    }
}

TEST_CASE("the scheme reaches the whole action bar") {
    // Six slots on the pad and six more behind the shift the left bumper
    // holds. Without the modifier in the table the other six are unreachable,
    // which is the kind of gap that is only noticed at level 40.
    std::size_t count = 0;
    const wowee::ui::PadBinding* bindings = wowee::ui::padBindings(count);
    int actionKeys = 0;
    bool hasModifier = false;
    for (std::size_t i = 0; i < count; ++i) {
        const SDL_Scancode key = bindings[i].key;
        if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_6) ++actionKeys;
        if (key == SDL_SCANCODE_LSHIFT) hasModifier = true;
    }
    CHECK(actionKeys == 6);
    CHECK(hasModifier);
}

TEST_CASE("the keys the pad holds are ones the client answers") {
    // The client polls these by scancode. A binding to a key nothing reads is
    // a button that does nothing, and the table is the only place that would
    // say so.
    std::size_t count = 0;
    const wowee::ui::PadBinding* bindings = wowee::ui::padBindings(count);
    const std::set<int> answered = {
        SDL_SCANCODE_SPACE, SDL_SCANCODE_TAB, SDL_SCANCODE_NUMLOCKCLEAR,
        SDL_SCANCODE_LSHIFT,
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
        SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
    };
    for (std::size_t i = 0; i < count; ++i) {
        INFO(bindings[i].what);
        CHECK(answered.count(static_cast<int>(bindings[i].key)) == 1);
    }
}

TEST_CASE("the pointer does not move on its own") {
    const glm::vec2 still = wowee::ui::GamepadControls::pointerStep(0.0f, 0.0f, 0.016f);
    CHECK(still.x == Catch::Approx(0.0f));
    CHECK(still.y == Catch::Approx(0.0f));
    // A frame that took no time moves it nowhere either, rather than dividing
    // by it.
    const glm::vec2 frozen = wowee::ui::GamepadControls::pointerStep(1.0f, 0.0f, 0.0f);
    CHECK(frozen.x == Catch::Approx(0.0f));
}

TEST_CASE("a gentle push moves the pointer much more slowly than a full one") {
    // The reason for the curve. Linear, the speed that can land on a small
    // button cannot cross a window, and the speed that crosses a window
    // cannot land on the button.
    const float dt = 1.0f;
    const float slow = wowee::ui::GamepadControls::pointerStep(0.25f, 0.0f, dt).x;
    const float fast = wowee::ui::GamepadControls::pointerStep(1.0f, 0.0f, dt).x;
    CHECK(slow > 0.0f);
    // A quarter of the stick is a sixteenth of the speed.
    CHECK(fast / slow == Catch::Approx(16.0f).margin(0.1f));
    // And a full push crosses a 1280 wide window in under two seconds.
    CHECK(fast > 640.0f);
}

TEST_CASE("the pointer goes where the stick points") {
    const glm::vec2 diagonal = wowee::ui::GamepadControls::pointerStep(0.5f, 0.5f, 0.1f);
    CHECK(diagonal.x == Catch::Approx(diagonal.y));
    CHECK(diagonal.x > 0.0f);
    const glm::vec2 up = wowee::ui::GamepadControls::pointerStep(0.0f, -0.8f, 0.1f);
    CHECK(up.x == Catch::Approx(0.0f));
    CHECK(up.y < 0.0f);
}

TEST_CASE("a stick reading past its corner does not outrun the curve") {
    // stickVector caps at one, but this is a static taking whatever it is
    // given, and a driver that reports 1.4 on the diagonal would otherwise
    // move the pointer twice as fast diagonally as straight.
    const float straight = wowee::ui::GamepadControls::pointerStep(1.0f, 0.0f, 0.1f).x;
    const glm::vec2 corner = wowee::ui::GamepadControls::pointerStep(1.0f, 1.0f, 0.1f);
    const float diagonal = std::sqrt(corner.x * corner.x + corner.y * corner.y);
    CHECK(diagonal == Catch::Approx(straight).margin(0.001f));
}

TEST_CASE("the pointer travels the same distance however the frame is cut") {
    // A step proportional to the frame time, so a 144Hz screen and a 30Hz one
    // move the pointer at the same speed.
    const float oneStep = wowee::ui::GamepadControls::pointerStep(0.6f, 0.0f, 0.2f).x;
    float many = 0.0f;
    for (int i = 0; i < 10; ++i) many += wowee::ui::GamepadControls::pointerStep(0.6f, 0.0f, 0.02f).x;
    CHECK(many == Catch::Approx(oneStep).margin(0.001f));
}

TEST_CASE("every bound button has a name a player would recognise") {
    // The settings panel lists the scheme off this table. A button with no
    // label is silently dropped from that list, which is how a control scheme
    // comes to be missing the one line someone was looking for.
    std::size_t count = 0;
    const wowee::ui::PadBinding* bindings = wowee::ui::padBindings(count);
    for (std::size_t i = 0; i < count; ++i) {
        INFO(bindings[i].what);
        const char* label = wowee::ui::padButtonLabel(bindings[i].button);
        REQUIRE(label != nullptr);
        CHECK(label[0] != '\0');
    }
    // And the two that are not in the table, because they go through ImGui
    // rather than through a scancode, are still named.
    CHECK(std::string(wowee::ui::padButtonLabel(SDL_CONTROLLER_BUTTON_B)) == "B");
    CHECK(std::string(wowee::ui::padButtonLabel(SDL_CONTROLLER_BUTTON_START)) == "Start");
    CHECK(std::string(wowee::ui::padButtonLabel(SDL_CONTROLLER_BUTTON_BACK)) == "Back");
}
