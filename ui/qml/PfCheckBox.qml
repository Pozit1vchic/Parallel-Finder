import QtQuick
import QtQuick.Controls.Basic
import PfUi

CheckBox {
    id: control
    implicitHeight: 28
    height: 28
    spacing: 9

    indicator: Rectangle {
        x: 0
        y: Math.round((control.height - height) / 2)
        width: 18
        height: 18
        radius: 4
        color: control.checked ? Theme.accent : Theme.canvas
        border.color: control.checked ? Theme.accent : Theme.hairline
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "✓"
            visible: control.checked
            color: Theme.canvas
            font.pixelSize: 13
            font.weight: Font.DemiBold
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
