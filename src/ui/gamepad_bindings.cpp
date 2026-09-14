#include "ui/gamepad_controls.hpp"

namespace wowee {
namespace ui {

namespace {

// The scheme. Read down the list and it is the whole of what a pad's buttons
// do, apart from Escape - which is on B and on Start, and goes through ImGui
// rather than through a scancode, because that is where the interface reads
// it.
//
// On its own in this file, and not beside the code that applies it, so that a
// test can check the table without linking the camera and ImGui behind it. It
// is data; the only questions worth asking of it - is a button bound twice, is
// a key, is every key one the client actually polls - are questions about the
// data alone.
constexpr PadBinding kBindings[] = {
    {SDL_CONTROLLER_BUTTON_A,             SDL_SCANCODE_SPACE,        "Jump"},
    {SDL_CONTROLLER_BUTTON_X,             SDL_SCANCODE_1,            "Action 1"},
    {SDL_CONTROLLER_BUTTON_Y,             SDL_SCANCODE_2,            "Action 2"},
    {SDL_CONTROLLER_BUTTON_DPAD_UP,       SDL_SCANCODE_3,            "Action 3"},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    SDL_SCANCODE_4,            "Action 4"},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN,     SDL_SCANCODE_5,            "Action 5"},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT,     SDL_SCANCODE_6,            "Action 6"},
    // Shift is not an action of its own: the client already reads it as "the
    // bottom-left bar", which is where actions 7 to 12 live on a keyboard
    // too. So the modifier costs nothing to implement and behaves exactly as
    // a player expects it to.
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  SDL_SCANCODE_LSHIFT,       "Hold: actions 7-12"},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_SCANCODE_TAB,          "Target nearest enemy"},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK,     SDL_SCANCODE_NUMLOCKCLEAR, "Autorun"},
};

}  // namespace

const char* padButtonLabel(SDL_GameControllerButton button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A:             return "A";
        case SDL_CONTROLLER_BUTTON_B:             return "B";
        case SDL_CONTROLLER_BUTTON_X:             return "X";
        case SDL_CONTROLLER_BUTTON_Y:             return "Y";
        case SDL_CONTROLLER_BUTTON_BACK:          return "Back";
        case SDL_CONTROLLER_BUTTON_START:         return "Start";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "Left stick click";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "Right stick click";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "Left bumper";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "Right bumper";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "D-pad up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "D-pad down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "D-pad left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "D-pad right";
        default:                                  return "";
    }
}

const PadBinding* padBindings(std::size_t& count) {
    count = sizeof(kBindings) / sizeof(kBindings[0]);
    return kBindings;
}

}  // namespace ui
}  // namespace wowee
