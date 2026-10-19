import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi

CheckBox {
    id: control
    property string tooltipText: ""
    implicitHeight: Math.max(28, contentItem.implicitHeight + 8)
    height: implicitHeight
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
            color: Theme.tooltipSurface
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
        Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }

        Canvas {
            anchors.centerIn: parent
            width: 14; height: 14
            property real progress: control.checked ? 1 : 0
            property color strokeColor: Theme.accentForeground
            onProgressChanged: requestPaint()
            onStrokeColorChanged: requestPaint()
            Behavior on progress { NumberAnimation { duration: Theme.reducedMotion ? 0 : 200; easing.type: Easing.OutCubic } }
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = strokeColor; ctx.lineWidth = 1.8
                ctx.lineCap = "round"; ctx.lineJoin = "round"
                ctx.beginPath(); ctx.moveTo(2.5, 7)
                const first = Math.min(1, progress / .35)
                ctx.lineTo(2.5 + 3 * first, 7 + 3 * first)
                if (progress > .35) {
                    const second = (progress - .35) / .65
                    ctx.lineTo(5.5 + 6 * second, 10 - 7 * second)
                }
                if (progress > 0) ctx.stroke()
            }
        }
    }

    contentItem: Text { font.family: Theme.fontFamily;
        text: control.text
        color: control.enabled ? Theme.textSecondary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        leftPadding: 27
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
