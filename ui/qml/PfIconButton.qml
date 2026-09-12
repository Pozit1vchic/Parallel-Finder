import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi

Button {
    id: control
    property url iconSource
    property string accessibleName: ""
    property color iconColor: Theme.textSecondary
    property int iconSize: 17
    implicitWidth: 32
    implicitHeight: 32
    width: implicitWidth
    height: implicitHeight
    hoverEnabled: true
    ToolTip.visible: hovered && accessibleName.length > 0
    ToolTip.text: accessibleName
    ToolTip.delay: 500

    contentItem: Image {
        source: control.iconSource
        sourceSize.width: control.iconSize
        sourceSize.height: control.iconSize
        fillMode: Image.PreserveAspectFit
        anchors.margins: 7
        layer.enabled: true
        layer.effect: MultiEffect {
            colorization: 1
            colorizationColor: control.down ? Theme.textPrimary : control.iconColor
        }
    }

    background: Rectangle {
        radius: Theme.radiusButton
        color: control.down ? Theme.accentMuted : control.hovered ? Theme.surfaceRaised : "transparent"
        border.width: control.hovered ? 1 : 0
        border.color: Theme.hairline
    }
}
