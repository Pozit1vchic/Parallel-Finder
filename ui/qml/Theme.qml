// Parallel-Finder design tokens — Claude/Anthropic style (spec section 5).
// Nothing is hardcoded in views: all colors/radii/spacings come from here.
pragma Singleton
import QtQuick

QtObject {
    // Palette
    readonly property color background: "#181715"       // app background
    readonly property color canvas: "#10100F"           // near-black editorial canvas
    readonly property color panel: "#1F1E1B"            // dark product surface
    readonly property color heroPanel: "#181715"        // elevated motion surface
    readonly property color panelAlt: "#252320"         // elevated inner surface
    readonly property color border: Qt.rgba(255, 255, 255, 0.06)
    readonly property color hairline: Qt.rgba(255, 255, 255, 0.10)
    readonly property color accent: "#CC785C"           // warm coral
    readonly property color accentMuted: Qt.rgba(0.80, 0.47, 0.36, 0.14)
    readonly property color sage: "#6D9B8F"             // restrained teal-sage
    readonly property color sageMuted: Qt.rgba(0.43, 0.61, 0.56, 0.14)
    readonly property color textPrimary: "#F5F0E8"
    readonly property color textSecondary: "#A09D96"
    readonly property color textDisabled: "#68655F"

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
