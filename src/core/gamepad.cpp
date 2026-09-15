#include "core/gamepad.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <cmath>

namespace wowee {
namespace core {

float axisFraction(int raw) {
    const float scaled = static_cast<float>(raw) / 32767.0f;
    return std::clamp(scaled, -1.0f, 1.0f);
}

glm::vec2 stickVector(float rawX, float rawY, float deadzone) {
    const float dz = std::clamp(deadzone, 0.0f, 0.95f);
    glm::vec2 v(rawX, rawY);
    const float length = std::sqrt(v.x * v.x + v.y * v.y);
    if (length <= dz || length <= 0.0f) return glm::vec2(0.0f);
    // Rescaled to start at zero outside the deadzone, and capped at one: a
    // square-cornered stick reads past 1 on the diagonal, and that would be a
    // character who walks faster north-east than north.
    const float scaled = std::min((length - dz) / (1.0f - dz), 1.0f);
    return v * (scaled / length);
}

float triggerFraction(float raw, float deadzone) {
    const float dz = std::clamp(deadzone, 0.0f, 0.95f);
    const float pull = std::clamp(raw, 0.0f, 1.0f);
    if (pull <= dz) return 0.0f;
    return std::min((pull - dz) / (1.0f - dz), 1.0f);
}

Gamepad& Gamepad::getInstance() {
    static Gamepad instance;
    return instance;
}

Gamepad& gamepad() { return Gamepad::getInstance(); }

bool Gamepad::init() {
    if (initialised_) return true;
    // The subsystem is asked for separately from video, because a client that
    // cannot talk to controllers must still start. SDL_INIT_GAMECONTROLLER
    // brings the joystick and event subsystems with it.
    // Face buttons by position, not by the letters printed on them.
    //
    // SDL's default is to report them by label, and a Nintendo pad's labels
    // are laid out the other way round: its A is where an Xbox pad's B is.
    // So the button this client calls A - the one that jumps - would have
    // been the right-hand button on a Switch pad and the bottom one
    // everywhere else, which is backwards from every game those players have
    // used. Positional here, and named per pad where a name is shown.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        LOG_WARNING("Gamepad: SDL's controller subsystem would not start: ", SDL_GetError(),
                    " - controllers will not be seen this session");
        return false;
    }
    initialised_ = true;
    // Events rather than polling, so a pad plugged in mid-session arrives on
    // the same queue as everything else.
    SDL_GameControllerEventState(SDL_ENABLE);
    openFirstAvailable();
    return true;
}

void Gamepad::shutdown() {
    closeDevice();
    if (initialised_) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        initialised_ = false;
    }
}

int Gamepad::addMappingsFromFile(const std::string& path) {
    if (path.empty()) return -1;
    const int added = SDL_GameControllerAddMappingsFromFile(path.c_str());
    if (added < 0) return -1;
    LOG_INFO("Gamepad: ", added, " controller mappings from ", path);
    return added;
}

void Gamepad::openFirstAvailable() {
    if (pad_) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) {
            // A joystick SDL has no mapping for. Named rather than skipped in
            // silence: "my controller does nothing" and "my controller is not
            // a controller SDL recognises" look identical from outside, and
            // the second is fixed by a mapping file rather than by this code.
            const char* joyName = SDL_JoystickNameForIndex(i);
            LOG_WARNING("Gamepad: ", joyName ? joyName : "a device",
                        " is not a mapped controller - it needs a line in a "
                        "gamecontrollerdb.txt to be usable");
            continue;
        }
        openDevice(i);
        if (pad_) return;
    }
}

namespace {

/// Lowercased, for a name compare that does not care how the driver wrote it.
std::string toLower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

Gamepad::Kind Gamepad::kindOf(SDL_GameController* pad) {
    if (!pad) return Kind::Unknown;
    // The Steam Deck has no type of its own in SDL, so it is recognised the
    // only way left: Valve's USB vendor and the Deck's own product id. It
    // matters because the Deck has four back buttons where most pads have
    // none, and because "A" on it means the Xbox A rather than the Nintendo
    // one.
    constexpr Uint16 kValve = 0x28DE;
    constexpr Uint16 kSteamDeck = 0x1205;
    if (SDL_GameControllerGetVendor(pad) == kValve &&
        SDL_GameControllerGetProduct(pad) == kSteamDeck) {
        return Kind::SteamDeck;
    }
    // And by name, because the Deck reaches SDL by more than one road: its
    // own driver gives Valve's ids, and the kernel's virtual pad gives a name
    // and little else. Neither is reliable on its own and the cost of asking
    // both is one string compare, once, when a pad is plugged in.
    if (const char* padName = SDL_GameControllerName(pad); padName) {
        const std::string lower = toLower(padName);
        if (lower.find("steam deck") != std::string::npos) return Kind::SteamDeck;
    }
    switch (SDL_GameControllerGetType(pad)) {
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE:
            return Kind::Xbox;
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5:
            return Kind::PlayStation;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return Kind::Nintendo;
        case SDL_CONTROLLER_TYPE_AMAZON_LUNA:  return Kind::Luna;
        case SDL_CONTROLLER_TYPE_GOOGLE_STADIA: return Kind::Stadia;
        case SDL_CONTROLLER_TYPE_NVIDIA_SHIELD: return Kind::Shield;
        case SDL_CONTROLLER_TYPE_VIRTUAL:       return Kind::Virtual;
        default:                                return Kind::Unknown;
    }
}

bool Gamepad::hasButton(SDL_GameControllerButton button) const {
    if (!pad_ || button < 0 || button >= kButtonCount) return false;
    return SDL_GameControllerHasButton(pad_, button) == SDL_TRUE;
}

void Gamepad::openDevice(int joystickIndex) {
    if (pad_) return;
    SDL_GameController* opened = SDL_GameControllerOpen(joystickIndex);
    if (!opened) {
        LOG_WARNING("Gamepad: could not open controller ", joystickIndex, ": ", SDL_GetError());
        return;
    }
    pad_ = opened;
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(pad_);
    instanceId_ = joystick ? SDL_JoystickInstanceID(joystick) : -1;
    const char* padName = SDL_GameControllerName(pad_);
    name_ = padName ? padName : "controller";
    kind_ = kindOf(pad_);
    current_.fill(false);
    touch_.fill(TouchFinger{});
    // Which touch surface to point with, when there is more than one.
    //
    // A PlayStation pad has one and this is 0. A Steam Deck has two, and the
    // right-hand one is the one a thumb points with - the left is where its
    // owner has put a scroll wheel or a d-pad. SDL numbers them left to
    // right, so the last is the right-hand one.
    const int touchpads = SDL_GameControllerGetNumTouchpads(pad_);
    touchpad_ = touchpads > 0 ? touchpads - 1 : 0;
    touchFingers_ = touchpads > 0
                        ? std::clamp(SDL_GameControllerGetNumTouchpadFingers(pad_, touchpad_),
                                     0, kTouchFingers)
                        : 0;
    LOG_INFO("Gamepad: ", name_, " connected", hasTouchpad() ? ", with a touchpad" : "");
}

void Gamepad::closeDevice() {
    if (!pad_) return;
    LOG_INFO("Gamepad: ", name_, " disconnected");
    SDL_GameControllerClose(pad_);
    pad_ = nullptr;
    kind_ = Kind::Unknown;
    instanceId_ = -1;
    name_.clear();
    // Zeroed rather than left as it was. A pad unplugged mid-stride would
    // otherwise leave its last reading standing, and the character would walk
    // north until something else stopped them.
    current_.fill(false);
    touch_.fill(TouchFinger{});
    touchFingers_ = 0;
    touchpad_ = 0;
    leftStick_ = glm::vec2(0.0f);
    rightStick_ = glm::vec2(0.0f);
    leftTrigger_ = 0.0f;
    rightTrigger_ = 0.0f;
}

void Gamepad::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_CONTROLLERDEVICEADDED:
            // `which` is a device index here, and an instance id on the other
            // two events. They are different numbers and SDL does not warn.
            openDevice(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (event.cdevice.which == instanceId_) {
                closeDevice();
                // Something else may still be plugged in - unplugging the
                // second of two pads should not end controller support.
                openFirstAvailable();
            }
            break;
        default:
            break;
    }
}

void Gamepad::update() {
    if (!pad_ || !SDL_GameControllerGetAttached(pad_)) {
        if (pad_) {
            // Attached went false without a removal event, which happens when
            // a pad sleeps or its receiver is pulled.
            closeDevice();
            openFirstAvailable();
        }
        return;
    }

    for (int i = 0; i < kButtonCount; ++i) {
        current_[static_cast<std::size_t>(i)] =
            SDL_GameControllerGetButton(pad_, static_cast<SDL_GameControllerButton>(i)) != 0;
    }

    leftStick_ = stickVector(axisFraction(SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX)),
                             axisFraction(SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY)),
                             stickDeadzone_);
    rightStick_ = stickVector(axisFraction(SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_RIGHTX)),
                              axisFraction(SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_RIGHTY)),
                              stickDeadzone_);
    leftTrigger_ = triggerFraction(
        axisFraction(SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_TRIGGERLEFT)), kTriggerDeadzone);
    rightTrigger_ = triggerFraction(
        axisFraction(SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)), kTriggerDeadzone);

    for (int f = 0; f < kTouchFingers; ++f) {
        TouchFinger& finger = touch_[static_cast<std::size_t>(f)];
        Uint8 state = 0;
        float x = 0.0f;
        float y = 0.0f;
        float pressure = 0.0f;
        const bool read = f < touchFingers_ &&
                          SDL_GameControllerGetTouchpadFinger(pad_, touchpad_, f, &state, &x, &y,
                                                              &pressure) == 0;
        finger.down = read && state != 0;
        if (finger.down) finger.position = glm::vec2(x, y);
    }
}

bool Gamepad::held(SDL_GameControllerButton button) const {
    if (button < 0 || button >= kButtonCount) return false;
    return current_[static_cast<std::size_t>(button)];
}

const Gamepad::TouchFinger& Gamepad::touch(int finger) const {
    static const TouchFinger kNone{};
    if (finger < 0 || finger >= kTouchFingers) return kNone;
    return touch_[static_cast<std::size_t>(finger)];
}

void Gamepad::setStickDeadzone(float fraction) {
    stickDeadzone_ = std::clamp(fraction, 0.0f, 0.9f);
}

std::string Gamepad::describe() const {
    if (!pad_) return "no controller connected";
    return name_ + " connected";
}

}  // namespace core
}  // namespace wowee
