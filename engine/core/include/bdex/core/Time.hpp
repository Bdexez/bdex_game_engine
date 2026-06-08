#pragma once

#include <bdex/core/Types.hpp>

namespace bdex {

/// Timing information handed to every scheduled task.
struct FrameTime {
    f64 delta = 0.0;    ///< Seconds since the previous frame (or the fixed step in FixedUpdate).
    f64 elapsed = 0.0;  ///< Seconds of simulated time since the engine started.
    u64 frame = 0;      ///< Index of the current frame, starting at 0.
};

}  // namespace bdex
