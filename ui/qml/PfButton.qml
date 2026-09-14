import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi

Button {
    id: control
    property bool quiet: false
    property bool sageAction: false
    property bool compact: false
    implicitHeight: compact ? 32 : 38
    height: implicitHeight
    padding: compact ? 6 : 14
    hoverEnabled: true

    contentItem: Text {
        text: control.text
        color: !control.enabled ? Theme.textDisabled
              : control.quiet ? Theme.textSecondary : Theme.canvas
        font.family: Theme.fontFamily
        font.pixelSize: control.compact ? 10 : 12
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideNone
    }

    background: Rectangle {
        radius: Theme.radiusButton
        color: !control.enabled ? Theme.surfaceMuted
              : control.down ? (control.sageAction ? Theme.sagePressed : Theme.accentPressed)
              : control.hovered ? (control.sageAction ? Theme.sageBright : Theme.accentBright)
              : control.quiet ? Theme.surfaceRaised : (control.sageAction ? Theme.sage : Theme.accent)
        border.width: control.activeFocus ? 2 : control.quiet ? 1 : 0
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.hairlineStrong : Theme.hairline
    }
}
