#include "core/gamepad.hpp"

#include "core/logger.hpp"

#include <algorithm>
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
    current_.fill(false);
    touch_.fill(TouchFinger{});
    touchFingers_ = SDL_GameControllerGetNumTouchpads(pad_) > 0
                        ? std::clamp(SDL_GameControllerGetNumTouchpadFingers(pad_, 0), 0, kTouchFingers)
                        : 0;
    LOG_INFO("Gamepad: ", name_, " connected", hasTouchpad() ? ", with a touchpad" : "");
}

void Gamepad::closeDevice() {
    if (!pad_) return;
    LOG_INFO("Gamepad: ", name_, " disconnected");
    SDL_GameControllerClose(pad_);
    pad_ = nullptr;
    instanceId_ = -1;
    name_.clear();
    // Zeroed rather than left as it was. A pad unplugged mid-stride would
    // otherwise leave its last reading standing, and the character would walk
    // north until something else stopped them.
    current_.fill(false);
    touch_.fill(TouchFinger{});
    touchFingers_ = 0;
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
                          SDL_GameControllerGetTouchpadFinger(pad_, 0, f, &state, &x, &y, &pressure) == 0;
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
