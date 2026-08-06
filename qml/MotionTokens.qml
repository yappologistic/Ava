pragma Singleton

import QtQuick 6.5

QtObject {
    readonly property int press: 85
    readonly property int hover: 135
    readonly property int state: 180
    readonly property int content: 220
    readonly property int reveal: 280
    readonly property int island: 340
    readonly property int directSettle: 160
    readonly property int activityHandoff: 210

    readonly property int easeOut: Easing.OutCubic
    readonly property int easeInOut: Easing.InOutCubic
    readonly property int settle: Easing.OutBack
}
