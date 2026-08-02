#pragma once

#include <bdex/core/Types.hpp>
#include <bdex/platform/KeyCodes.hpp>

namespace bdex::platform {

class IWindow;

// Events published on the engine EventBus by window modules.

struct WindowCloseRequested {
    IWindow* window = nullptr;
};

struct WindowResized {
    IWindow* window = nullptr;
    Extent2D framebufferSize;
};

struct WindowFocusChanged {
    IWindow* window = nullptr;
    bool focused = false;
};

struct KeyEvent {
    Key key = Key::Unknown;
    KeyAction action = KeyAction::Press;
};

struct MouseButtonEvent {
    MouseButton button = MouseButton::Left;
    bool pressed = false;
};

struct MouseMoved {
    f64 x = 0.0;
    f64 y = 0.0;
};

struct MouseScrolled {
    f64 deltaX = 0.0;
    f64 deltaY = 0.0;
};

}  // namespace bdex::platform
