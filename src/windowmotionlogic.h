#pragma once

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace WindowMotionLogic {

inline qreal framePeriodMilliseconds(qreal refreshRateHz)
{
    const qreal boundedRate = std::isfinite(refreshRateHz)
        ? std::clamp(refreshRateHz, 24.0, 500.0)
        : 60.0;
    return 1000.0 / boundedRate;
}

// QTimer accepts whole milliseconds. Carry the rounding error forward so a
// 120 Hz display produces an 8/9/8 ms rhythm and 144 Hz produces the matching
// 6/7 ms rhythm instead of drifting at a fixed rounded interval.
inline int nextTimerIntervalMilliseconds(qreal exactPeriodMilliseconds,
                                         qreal &roundingErrorMilliseconds)
{
    const qreal requested = std::max(1.0, exactPeriodMilliseconds)
        + roundingErrorMilliseconds;
    const int interval = std::max(1, qRound(requested));
    roundingErrorMilliseconds = requested - interval;
    return interval;
}

// Moving a native window is cheap; asking an arbitrary application to fully
// relayout its client area can be much more expensive. Preserve display-rate
// translation while capping intermediate resize commits near 60-90 Hz.
inline int resizeCommitCadence(qreal refreshRateHz, int applicationCadence)
{
    const qreal boundedRate = std::isfinite(refreshRateHz)
        ? std::clamp(refreshRateHz, 24.0, 500.0)
        : 60.0;
    const int displayCadence = std::clamp(
        static_cast<int>(std::ceil(boundedRate / 90.0)),
        1,
        3);
    return std::clamp(std::max(applicationCadence, displayCadence), 1, 3);
}

} // namespace WindowMotionLogic
