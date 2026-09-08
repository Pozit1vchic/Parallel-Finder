// Parallel-Finder design tokens — Claude/Anthropic style (spec section 5).
// Nothing is hardcoded in views: all colors/radii/spacings come from here.
pragma Singleton
import QtQuick

QtObject {
    // Palette
    readonly property color background: "#262624"       // app background
    readonly property color panel: "#30302E"            // panels
    readonly property color panelAlt: "#353533"         // hover / nested panels
    readonly property color border: Qt.rgba(255, 255, 255, 0.06)
    readonly property color accent: "#D97757"           // terracotta
    readonly property color accentMuted: Qt.rgba(0.851, 0.467, 0.341, 0.16)
    readonly property color sage: "#7C9885"             // second accent
    readonly property color sageMuted: Qt.rgba(0.486, 0.596, 0.522, 0.16)
    readonly property color textPrimary: "#F5F1EC"
    readonly property color textSecondary: "#B0AEA6"
    readonly property color textDisabled: "#6E6C66"

    // Typography
    readonly property string fontFamily: "Segoe UI"
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeBody: 13
    readonly property int fontSizeTitle: 18

    // Radii: buttons 8-10, cards 10-12, splash 16
    readonly property int radiusButton: 8
    readonly property int radiusCard: 12
    readonly property int radiusSplash: 16

    // Shadows: blur 14-24, opacity 0.08-0.35
    readonly property int shadowBlurPanel: 16
    readonly property int shadowBlurOverlay: 24
    readonly property real shadowOpacity: 0.12

    // Icon line width (Lucide, line-style)
    readonly property real iconStrokeWidth: 1.6

    // Layout
    readonly property int topBarHeight: 48
    readonly property int sidePanelWidth: 300
    readonly property int resultRowHeight: 32 // virtualized rows
    readonly property int margins: 12
}
