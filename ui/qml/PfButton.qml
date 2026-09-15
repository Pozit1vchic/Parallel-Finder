import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import PfUi

Button {
    id: control
    property bool quiet: false
    property bool sageAction: false
    property bool compact: false
    // Two-line labels are intentional in the narrow rail.  Give the text a
    // real second line instead of letting the control clip it at 32/38 px.
    implicitHeight: compact ? 36 : 46
    height: implicitHeight
    padding: compact ? 6 : 12
    leftPadding: compact ? 7 : 12
    rightPadding: compact ? 7 : 12
    topPadding: compact ? 5 : 6
    bottomPadding: compact ? 5 : 6
    hoverEnabled: true
    activeFocusOnTab: true
    Layout.minimumWidth: 0

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
        elide: Text.ElideNone
        clip: true
    }

    background: Rectangle {
        radius: Theme.radiusButton
        color: !control.enabled ? Theme.surfaceMuted
              : control.down ? (control.sageAction ? Theme.sagePressed : Theme.accentPressed)
              : control.hovered ? (control.sageAction ? Theme.sageBright : Theme.accentBright)
              : control.quiet ? Theme.surfaceRaised : (control.sageAction ? Theme.sage : Theme.accent)
        border.width: control.visualFocus ? 2 : control.quiet ? 1 : 0
        border.color: control.visualFocus ? Theme.accent : control.hovered ? Theme.hairlineStrong : Theme.hairline
    }
}
