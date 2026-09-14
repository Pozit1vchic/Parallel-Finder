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
    leftPadding: compact ? 7 : 14
    rightPadding: compact ? 7 : 14
    topPadding: compact ? 4 : 7
    bottomPadding: compact ? 4 : 7
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
        // A button is a control, not a marquee: let longer localized labels
        // wrap inside the available width instead of silently cutting them.
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        clip: true
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
