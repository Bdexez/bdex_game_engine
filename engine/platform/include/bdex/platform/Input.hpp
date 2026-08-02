#pragma once

#include <bdex/core/EventBus.hpp>
#include <bdex/core/Math.hpp>
#include <bdex/platform/KeyCodes.hpp>

#include <bitset>

namespace bdex::platform {

/// Polled view of keyboard and mouse state, built from the input events on
/// the EventBus. It is backend-agnostic: any window module (or a test, or a
/// replay system) that publishes KeyEvent / MouseMoved / ... drives it.
///
/// Window modules provide it as a service and call beginFrame() right before
/// pumping OS events.
class InputState {
public:
    explicit InputState(EventBus& events);

    /// Clears the per-frame transitions (pressed/released, deltas).
    void beginFrame();

    [[nodiscard]] bool keyDown(Key key) const;
    /// True only during the frame the key went down.
    [[nodiscard]] bool keyPressed(Key key) const;
    /// True only during the frame the key went up.
    [[nodiscard]] bool keyReleased(Key key) const;

    [[nodiscard]] bool mouseButtonDown(MouseButton button) const;
    [[nodiscard]] bool mouseButtonPressed(MouseButton button) const;

    [[nodiscard]] Vec2 mousePosition() const { return mousePosition_; }
    /// Cursor motion accumulated since beginFrame().
    [[nodiscard]] Vec2 mouseDelta() const { return mouseDelta_; }
    [[nodiscard]] Vec2 scrollDelta() const { return scrollDelta_; }

private:
    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);
    static constexpr usize kButtonCount = static_cast<usize>(MouseButton::Count);

    std::bitset<kKeyCount> keysDown_;
    std::bitset<kKeyCount> keysPressed_;
    std::bitset<kKeyCount> keysReleased_;
    std::bitset<kButtonCount> buttonsDown_;
    std::bitset<kButtonCount> buttonsPressed_;
    Vec2 mousePosition_{0.0f};
    Vec2 mouseDelta_{0.0f};
    Vec2 scrollDelta_{0.0f};
    bool hasMousePosition_ = false;

    Subscription keySubscription_;
    Subscription buttonSubscription_;
    Subscription moveSubscription_;
    Subscription scrollSubscription_;
};

}  // namespace bdex::platform
