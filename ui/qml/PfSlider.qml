import QtQuick
import QtQuick.Controls.Basic
import PfUi

Slider {
    id: control
    implicitHeight: 22
    height: 22

    background: Rectangle {
        x: 0
        y: Math.round(control.height / 2 - height / 2)
        width: control.width
        height: 4
        radius: 2
        color: Theme.panelAlt

        Rectangle {
            width: control.visualPosition * parent.width
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
        radius: 7
        color: Theme.canvas
        border.color: control.pressed ? Theme.textPrimary : Theme.accent
        border.width: 1
        layer.enabled: control.pressed
    }
}
