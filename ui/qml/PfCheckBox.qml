import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi

CheckBox {
    id: control
    property string tooltipText: ""
    implicitHeight: 28
    height: 28
    spacing: 9

    Rectangle {
        id: hint
        visible: control.hovered && control.tooltipText.length > 0
        x: 0
        y: control.height + 6
        z: 30
        width: 250
        height: hintText.implicitHeight + 16
        radius: Theme.radiusButton
        color: Theme.surfaceRaised
        border.color: Theme.hairlineStrong
        Text {
            id: hintText
            anchors.fill: parent
            anchors.margins: 8
            text: control.tooltipText
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 10
            lineHeight: 1.15
            wrapMode: Text.WordWrap
        }
    }

    indicator: Rectangle {
        x: 0
        y: Math.round((control.height - height) / 2)
        width: 18
        height: 18
        radius: 4
        color: control.checked ? Theme.accent : Theme.canvas
        border.color: control.activeFocus ? Theme.accent : control.checked ? Theme.accent : Theme.hairline
        border.width: control.activeFocus ? 2 : 1

        Image {
            anchors.centerIn: parent
            source: "qrc:/qt/qml/PfUi/qml/assets/check.svg"
            sourceSize.width: 13
            sourceSize.height: 13
            visible: control.checked
            layer.enabled: true
            layer.effect: MultiEffect { colorization: 1; colorizationColor: Theme.canvas }
        }
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.textSecondary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        leftPadding: 27
        font.pixelSize: 11
    }
}
