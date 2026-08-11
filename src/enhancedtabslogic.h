#pragma once

#include <QtGlobal>

namespace EnhancedTabsLogic {

inline int steppedIndex(int index, int delta, int count)
{
    if (count <= 0) {
        return -1;
    }
    const int wrapped = (index + delta) % count;
    return wrapped < 0 ? wrapped + count : wrapped;
}

inline int relativeDistance(int index, int selected, int count)
{
    if (count <= 0 || index < 0 || selected < 0) {
        return 0;
    }
    int distance = index - selected;
    const int half = count / 2;
    if (distance > half) {
        distance -= count;
    } else if (distance < -half) {
        distance += count;
    }
    return distance;
}

} // namespace EnhancedTabsLogic
