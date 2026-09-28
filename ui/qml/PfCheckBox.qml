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

    ToolTip {
        id: hint
        visible: control.hovered && control.tooltipText.length > 0
        delay: 350
        timeout: 8000

        // Keep the tooltip inside the application overlay rather than inside
        // the 28 px checkbox item. This prevents it from colliding with the
        // following checkboxes and avoids clipping by parent layouts.
        x: 0
        y: control.height + 6

        width: 250

        contentItem: Text {
            text: control.tooltipText
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 10
            lineHeight: 1.15
            wrapMode: Text.WordWrap
        }

        background: Rectangle {
            radius: Theme.radiusButton
            // Do not use Theme.surfaceRaised here: it can be translucent,
            // making labels underneath visually blend into the tooltip text.
            color: "#1C1C22"
            border.color: Theme.hairlineStrong
            border.width: 1
        }

        leftPadding: 8
        rightPadding: 8
        topPadding: 8
        bottomPadding: 8
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
            layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
            layer.effect: MultiEffect { colorization: 1; colorizationColor: Theme.canvas }
        }
    }

    contentItem: Text { font.family: Theme.fontFamily;
        text: control.text
        color: control.enabled ? Theme.textSecondary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        leftPadding: 27
        font.pixelSize: 11
    }
}
