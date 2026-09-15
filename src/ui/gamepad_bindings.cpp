#include "ui/gamepad_controls.hpp"

#include "core/gamepad.hpp"

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

namespace {

/// The names an Xbox pad puts on its buttons, which is the shape SDL resolves
/// every other pad to and so the fallback for anything with no names of its
/// own.
const char* xboxLabel(SDL_GameControllerButton button) {
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
        case SDL_CONTROLLER_BUTTON_MISC1:         return "Share";
        // SDL names the paddles by where they sit on the back of the pad
        // rather than by the letters on them: 1 is upper left, 2 upper right,
        // 3 lower left, 4 lower right. An Elite pad prints P1, P3, P2, P4 in
        // that order, which is why these read out of sequence.
        case SDL_CONTROLLER_BUTTON_PADDLE1:       return "Paddle P1";
        case SDL_CONTROLLER_BUTTON_PADDLE2:       return "Paddle P3";
        case SDL_CONTROLLER_BUTTON_PADDLE3:       return "Paddle P2";
        case SDL_CONTROLLER_BUTTON_PADDLE4:       return "Paddle P4";
        case SDL_CONTROLLER_BUTTON_TOUCHPAD:      return "Touchpad";
        default:                                  return "";
    }
}

}  // namespace

const char* padButtonLabel(SDL_GameControllerButton button, core::Gamepad::Kind kind) {
    using Kind = core::Gamepad::Kind;
    // Only what differs. The face buttons are read by position - see the hint
    // set where the subsystem starts - so the button named here is always the
    // same button under the same thumb, whatever is printed on it.
    switch (kind) {
        case Kind::PlayStation:
            switch (button) {
                case SDL_CONTROLLER_BUTTON_A:             return "Cross";
                case SDL_CONTROLLER_BUTTON_B:             return "Circle";
                case SDL_CONTROLLER_BUTTON_X:             return "Square";
                case SDL_CONTROLLER_BUTTON_Y:             return "Triangle";
                case SDL_CONTROLLER_BUTTON_BACK:          return "Share";
                case SDL_CONTROLLER_BUTTON_START:         return "Options";
                case SDL_CONTROLLER_BUTTON_GUIDE:         return "PS button";
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "L1";
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R1";
                case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "L3";
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "R3";
                case SDL_CONTROLLER_BUTTON_MISC1:         return "Microphone";
                default: break;
            }
            break;
        case Kind::Nintendo:
            // Nintendo prints its letters the other way round: the bottom
            // button is B and the right one is A, the left is Y and the top
            // is X. Read by position and named by what is printed there, so
            // the button a player is told to press is the one under their
            // thumb.
            switch (button) {
                case SDL_CONTROLLER_BUTTON_A:             return "B";
                case SDL_CONTROLLER_BUTTON_B:             return "A";
                case SDL_CONTROLLER_BUTTON_X:             return "Y";
                case SDL_CONTROLLER_BUTTON_Y:             return "X";
                case SDL_CONTROLLER_BUTTON_BACK:          return "Minus";
                case SDL_CONTROLLER_BUTTON_START:         return "Plus";
                case SDL_CONTROLLER_BUTTON_GUIDE:         return "Home";
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "L";
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R";
                case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "Left stick click";
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "Right stick click";
                case SDL_CONTROLLER_BUTTON_MISC1:         return "Capture";
                default: break;
            }
            break;
        case Kind::SteamDeck:
            // Xbox letters on the face, and four buttons on the back that
            // SDL reports in its own geometric order - upper left, upper
            // right, lower left, lower right - which on a Deck is L4, R4, L5,
            // R5.
            switch (button) {
                case SDL_CONTROLLER_BUTTON_BACK:          return "View";
                case SDL_CONTROLLER_BUTTON_START:         return "Menu";
                case SDL_CONTROLLER_BUTTON_GUIDE:         return "Steam";
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "L1";
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R1";
                case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "L3";
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "R3";
                case SDL_CONTROLLER_BUTTON_PADDLE1:       return "L4";
                case SDL_CONTROLLER_BUTTON_PADDLE2:       return "R4";
                case SDL_CONTROLLER_BUTTON_PADDLE3:       return "L5";
                case SDL_CONTROLLER_BUTTON_PADDLE4:       return "R5";
                case SDL_CONTROLLER_BUTTON_MISC1:         return "Quick access";
                default: break;
            }
            break;
        case Kind::Luna:
            if (button == SDL_CONTROLLER_BUTTON_MISC1) return "Microphone";
            break;
        case Kind::Xbox:
        case Kind::Stadia:
        case Kind::Shield:
        case Kind::Virtual:
        case Kind::Unknown:
            break;
    }
    return xboxLabel(button);
}

const char* padKeyName(SDL_GameControllerButton button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A:             return "PAD1";
        case SDL_CONTROLLER_BUTTON_B:             return "PAD2";
        case SDL_CONTROLLER_BUTTON_X:             return "PAD3";
        case SDL_CONTROLLER_BUTTON_Y:             return "PAD4";
        case SDL_CONTROLLER_BUTTON_BACK:          return "PADBACK";
        case SDL_CONTROLLER_BUTTON_GUIDE:         return "PADSYSTEM";
        case SDL_CONTROLLER_BUTTON_START:         return "PADFORWARD";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "PADLSTICK";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "PADRSTICK";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "PADLSHOULDER";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "PADRSHOULDER";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "PADDUP";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "PADDDOWN";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "PADDLEFT";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "PADDRIGHT";
        case SDL_CONTROLLER_BUTTON_MISC1:         return "PADSOCIAL";
        case SDL_CONTROLLER_BUTTON_PADDLE1:       return "PADPADDLE1";
        case SDL_CONTROLLER_BUTTON_PADDLE2:       return "PADPADDLE2";
        case SDL_CONTROLLER_BUTTON_PADDLE3:       return "PADPADDLE3";
        case SDL_CONTROLLER_BUTTON_PADDLE4:       return "PADPADDLE4";
        default:                                  return "";
    }
}

SDL_Scancode padClientKeyFor(const std::string& command) {
    // The poll sites' own keys: movement and jumping in the camera controller,
    // the action bar, Tab and Print Screen in GameScreen. The order of the
    // action buttons is the bar's slot order.
    static constexpr struct {
        const char* command;
        SDL_Scancode key;
    } kPolled[] = {
        {"MOVEFORWARD",        SDL_SCANCODE_W},
        {"MOVEBACKWARD",       SDL_SCANCODE_S},
        {"TURNLEFT",           SDL_SCANCODE_A},
        {"TURNRIGHT",          SDL_SCANCODE_D},
        {"STRAFELEFT",         SDL_SCANCODE_Q},
        {"STRAFERIGHT",        SDL_SCANCODE_E},
        {"JUMP",               SDL_SCANCODE_SPACE},
        {"TOGGLEAUTORUN",      SDL_SCANCODE_NUMLOCKCLEAR},
        {"TARGETNEARESTENEMY", SDL_SCANCODE_TAB},
        {"SCREENSHOT",         SDL_SCANCODE_PRINTSCREEN},
        {"ACTIONBUTTON1",      SDL_SCANCODE_1},
        {"ACTIONBUTTON2",      SDL_SCANCODE_2},
        {"ACTIONBUTTON3",      SDL_SCANCODE_3},
        {"ACTIONBUTTON4",      SDL_SCANCODE_4},
        {"ACTIONBUTTON5",      SDL_SCANCODE_5},
        {"ACTIONBUTTON6",      SDL_SCANCODE_6},
        {"ACTIONBUTTON7",      SDL_SCANCODE_7},
        {"ACTIONBUTTON8",      SDL_SCANCODE_8},
        {"ACTIONBUTTON9",      SDL_SCANCODE_9},
        {"ACTIONBUTTON10",     SDL_SCANCODE_0},
        {"ACTIONBUTTON11",     SDL_SCANCODE_MINUS},
        {"ACTIONBUTTON12",     SDL_SCANCODE_EQUALS},
    };
    for (const auto& polled : kPolled) {
        if (command == polled.command) return polled.key;
    }
    return SDL_SCANCODE_UNKNOWN;
}

namespace {

// The buttons only some pads carry, offered to the pads that have them.
//
// Four paddles and a share button are worth a great deal to a client whose
// action bar has twelve slots and whose pad reaches six of them without a
// modifier: a Steam Deck's four back buttons, or an Elite pad's, take the
// next four slots with nothing held down. Bound by SDL's own geometric order
// - upper left, upper right, lower left, lower right - so the top pair are
// the two nearest the index fingers on every pad that has them.
//
// Nothing here is in the base table, because a button that is not on the pad
// is a row in a settings list that does not exist and a key that can never be
// pressed.
constexpr PadBinding kExtras[] = {
    {SDL_CONTROLLER_BUTTON_PADDLE1, SDL_SCANCODE_7,          "Action 7"},
    {SDL_CONTROLLER_BUTTON_PADDLE2, SDL_SCANCODE_8,          "Action 8"},
    {SDL_CONTROLLER_BUTTON_PADDLE3, SDL_SCANCODE_9,          "Action 9"},
    {SDL_CONTROLLER_BUTTON_PADDLE4, SDL_SCANCODE_0,          "Action 10"},
    // The share, capture and microphone buttons are all one button to SDL,
    // and on every pad that has one it is the button for keeping a moment.
    {SDL_CONTROLLER_BUTTON_MISC1,   SDL_SCANCODE_PRINTSCREEN, "Screenshot"},
};

}  // namespace

const PadBinding* padExtraBindings(std::size_t& count) {
    count = sizeof(kExtras) / sizeof(kExtras[0]);
    return kExtras;
}

const PadBinding* padBindings(std::size_t& count) {
    count = sizeof(kBindings) / sizeof(kBindings[0]);
    return kBindings;
}

}  // namespace ui
}  // namespace wowee
