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
    ToolTip {
        visible: control.hovered && control.accessibleName.length > 0
        delay: 500
        contentItem: Text { text: control.accessibleName; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11 }
        background: Rectangle { color: Theme.tooltipSurface; radius: 7; border.color: Theme.hairlineStrong }
    }

    contentItem: Canvas {
        id: iconCanvas
        property url imageSource: control.iconSource
        property color tint: control.enabled ? control.iconColor : Theme.textDisabled
        onImageSourceChanged: loadImage(String(imageSource))
        onTintChanged: requestPaint()
        Component.onCompleted: loadImage(String(imageSource))
        onImageLoaded: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.globalCompositeOperation = "source-over"
            ctx.clearRect(0, 0, width, height)
            if (!isImageLoaded(String(imageSource))) return
            const size = Math.min(width, height, control.iconSize)
            ctx.drawImage(String(imageSource), (width-size)/2, (height-size)/2, size, size)
            ctx.globalCompositeOperation = "source-in"
            ctx.fillStyle = tint; ctx.fillRect(0, 0, width, height)
        }
        rotation: control.iconRotation
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
