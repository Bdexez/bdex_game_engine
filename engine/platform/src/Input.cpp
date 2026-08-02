#include <bdex/platform/Input.hpp>

#include <bdex/platform/Events.hpp>

namespace bdex::platform {

InputState::InputState(EventBus& events) {
    keySubscription_ = events.subscribe<KeyEvent>([this](const KeyEvent& event) {
        const usize index = static_cast<usize>(event.key);
        if (event.key == Key::Unknown || index >= kKeyCount) {
            return;
        }
        if (event.action == KeyAction::Press) {
            keysDown_.set(index);
            keysPressed_.set(index);
        } else if (event.action == KeyAction::Release) {
            keysDown_.reset(index);
            keysReleased_.set(index);
        }
    });

    buttonSubscription_ = events.subscribe<MouseButtonEvent>([this](const MouseButtonEvent& event) {
        const usize index = static_cast<usize>(event.button);
        if (index >= kButtonCount) {
            return;
        }
        buttonsDown_.set(index, event.pressed);
        if (event.pressed) {
            buttonsPressed_.set(index);
        }
    });

    moveSubscription_ = events.subscribe<MouseMoved>([this](const MouseMoved& event) {
        const Vec2 position(static_cast<f32>(event.x), static_cast<f32>(event.y));
        if (hasMousePosition_) {
            mouseDelta_ += position - mousePosition_;
        }
        // The first sample only establishes the origin: no jump on startup.
        mousePosition_ = position;
        hasMousePosition_ = true;
    });

    scrollSubscription_ = events.subscribe<MouseScrolled>([this](const MouseScrolled& event) {
        scrollDelta_ += Vec2(static_cast<f32>(event.deltaX), static_cast<f32>(event.deltaY));
    });
}

void InputState::beginFrame() {
    keysPressed_.reset();
    keysReleased_.reset();
    buttonsPressed_.reset();
    mouseDelta_ = Vec2(0.0f);
    scrollDelta_ = Vec2(0.0f);
}

bool InputState::keyDown(Key key) const {
    const usize index = static_cast<usize>(key);
    return index < kKeyCount && keysDown_.test(index);
}

bool InputState::keyPressed(Key key) const {
    const usize index = static_cast<usize>(key);
    return index < kKeyCount && keysPressed_.test(index);
}

bool InputState::keyReleased(Key key) const {
    const usize index = static_cast<usize>(key);
    return index < kKeyCount && keysReleased_.test(index);
}

bool InputState::mouseButtonDown(MouseButton button) const {
    const usize index = static_cast<usize>(button);
    return index < kButtonCount && buttonsDown_.test(index);
}

bool InputState::mouseButtonPressed(MouseButton button) const {
    const usize index = static_cast<usize>(button);
    return index < kButtonCount && buttonsPressed_.test(index);
}

}  // namespace bdex::platform
