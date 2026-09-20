import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi

Slider {
    id: control
    property string tooltipText: ""
    implicitHeight: 28
    height: 28

    background: Rectangle {
        x: 0
        y: Math.round(control.height / 2 - height / 2)
        width: control.width
        height: 5
        radius: 3
        color: Theme.surfaceMuted

        Rectangle {
            width: Math.max(0, control.visualPosition * parent.width)
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.visualPosition * (control.width - width)
        y: Math.round(control.height / 2 - height / 2)
        width: 14
        height: 14
        radius: 5
        color: Theme.textPrimary
        border.color: control.activeFocus ? Theme.textPrimary : control.pressed ? Theme.textPrimary : Theme.accent
        border.width: 1
        scale: control.pressed ? 1.1 : 1
        Behavior on scale { NumberAnimation { duration: Theme.motionDuration } }
    }
}
