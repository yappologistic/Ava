pragma Singleton

import QtQuick 6.5

QtObject {
    // Short, shared motion tiers keep the island feeling like one object instead
    // of a collection of controls animating at unrelated speeds.
    readonly property int press: 80
    readonly property int hover: 140
    readonly property int state: 180
    readonly property int content: 220
    readonly property int reveal: 260
    readonly property int island: 320
    readonly property int directSettle: 180
    readonly property int activityHandoff: 200

    readonly property int easeOut: Easing.OutQuart
    readonly property int easeInOut: Easing.InOutCubic
    // Ordinary UI state changes should arrive cleanly. Expressive overshoot is
    // reserved for explicit confirmations instead of every icon and menu item.
    readonly property int settle: Easing.OutQuint
}
