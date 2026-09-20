import QtQuick
import QtQuick.Controls.Basic
import PfUi

Button {
    id: control
    property url iconSource
    property string accessibleName: ""
    property color iconColor: Theme.textSecondary
    property int iconSize: 17
    property real iconRotation: 0
    scale: down && !Theme.reducedMotion ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    implicitWidth: 32
    implicitHeight: 32
    width: implicitWidth
    height: implicitHeight
    hoverEnabled: true
    // Icon buttons should not steal the initial focus when a modal opens.
    // Keyboard navigation can still reach them when the user tabs explicitly.
    activeFocusOnTab: true
    ToolTip.visible: hovered && accessibleName.length > 0
    ToolTip.text: accessibleName
    ToolTip.delay: 500

    contentItem: Image {
        source: control.iconSource
        rotation: control.iconRotation
        sourceSize.width: control.iconSize
        sourceSize.height: control.iconSize
        fillMode: Image.PreserveAspectFit
        anchors.margins: 7
        opacity: control.enabled ? (control.hovered ? 1 : 0.75) : 0.3
        Behavior on opacity { NumberAnimation { duration: Theme.motionDuration } }
    }

    background: Rectangle {
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
        radius: Theme.radiusButton
        color: control.down ? Theme.accentMuted : control.hovered ? Theme.surfaceRaised : "transparent"
        border.width: control.visualFocus ? 2 : control.hovered ? 1 : 0
        border.color: control.visualFocus ? Theme.accent : Theme.hairline
    }
}
