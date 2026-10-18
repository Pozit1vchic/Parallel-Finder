import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import PfUi

Button {
    id: control
    property bool quiet: false
    property bool primary: false
    property bool selected: false
    property bool sageAction: false
    property bool compact: false
    // Keep dense lists calm; reserve scale motion for emphasized actions.
    property bool motionEmphasis: false
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
    scale: Theme.reducedMotion || (!primary && !motionEmphasis) ? 1
        : down ? 0.98 : hovered && enabled ? 1.012 : 1
    Behavior on scale { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    Layout.minimumWidth: 0

    contentItem: Text {
        text: control.text
        color: !control.enabled ? Theme.textDisabled
              : control.primary ? Theme.accentForeground : control.selected ? Theme.accent : Theme.textPrimary
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
        id: surface
        radius: Theme.radiusButton
        readonly property color primaryFill: !control.enabled ? Theme.surfaceMuted
            : control.down ? Theme.accentPressed : control.hovered ? Theme.accentBright : Theme.accent
        readonly property color secondaryFill: !control.enabled ? Theme.surfaceMuted
            : control.down ? Theme.sagePressed : control.hovered ? Theme.sageBright : Theme.sage
        color: !control.enabled ? Theme.surfaceMuted
              : control.primary ? primaryFill
              : control.down || control.selected ? Theme.accentMuted : control.hovered ? Theme.surfaceMuted : control.quiet ? Theme.well : Theme.surfaceRaised
        // Keeping the stops collapsed outside gradient mode means the same
        // button component handles both a solid accent and a two-tone theme.
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: control.primary && Theme.accentGradientEnabled ? surface.primaryFill : surface.color
            }
            GradientStop {
                position: 1
                color: control.primary && Theme.accentGradientEnabled ? surface.secondaryFill : surface.color
            }
        }
        border.width: control.visualFocus ? 2 : 1
        border.color: !control.enabled ? Theme.hairline : control.visualFocus || control.selected ? Theme.accent : Theme.hairlineStrong
        Behavior on color { ColorAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    }
}
