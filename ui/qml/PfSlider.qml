import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi

Slider {
    id: control
    implicitHeight: 18
    height: 18

    background: Rectangle {
        x: 0
        y: Math.round(control.height / 2 - height / 2)
        width: control.width
        height: 3
        radius: 2
        color: Theme.panelAlt

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
        width: 12
        height: 12
        radius: 7
        color: Theme.canvas
        border.color: control.activeFocus ? Theme.textPrimary : control.pressed ? Theme.textPrimary : Theme.accent
        border.width: 1
        layer.enabled: control.pressed || control.hovered
        layer.effect: MultiEffect {
            shadowEnabled: control.pressed || control.hovered
            shadowColor: Theme.glowA
            shadowBlur: 0.55
            shadowVerticalOffset: 0
        }
    }
}
