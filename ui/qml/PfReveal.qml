import QtQuick
import PfUi

// Finite reveal: transforms do not alter layout or intercept input.
Item {
    id: root
    property bool active: false
    property int delay: 0
    property real distance: 10
    property real reveal: 1
    property bool complete: false
    opacity: reveal
    transform: Translate { y: root.distance * (1 - root.reveal) }
    function restart() {
        entrance.stop()
        if (!active || Theme.reducedMotion) { reveal = 1; return }
        reveal = 0
        entrance.start()
    }
    onActiveChanged: if (complete) restart()
    Component.onCompleted: { complete = true; restart() }
    Connections { target: Theme; function onReducedMotionChanged() { root.restart() } }
    SequentialAnimation {
        id: entrance
        PauseAnimation { duration: Theme.reducedMotion ? 0 : root.delay }
        NumberAnimation { target: root; property: "reveal"; to: 1; duration: Theme.motionRevealDuration; easing.type: Easing.OutQuint }
    }
}
