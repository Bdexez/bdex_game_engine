#pragma once

#include <bdex/core/Types.hpp>

namespace bdex::platform {

/// Keyboard keys, named after their position on a US layout.
/// Values are engine-defined; window backends translate their native codes.
enum class Key : u16 {
    Unknown = 0,

    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon = 59,
    Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,

    Escape = 256,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Right,
    Left,
    Down,
    Up,
    PageUp,
    PageDown,
    Home,
    End,
    CapsLock = 280,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift = 340,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper,

    Count = 352,
};

enum class MouseButton : u8 { Left = 0, Right, Middle, Button4, Button5, Count };

enum class KeyAction : u8 { Release, Press, Repeat };

}  // namespace bdex::platform
