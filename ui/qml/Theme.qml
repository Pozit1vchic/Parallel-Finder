// Parallel-Finder design tokens — Claude/Anthropic style (spec section 5).
// Nothing is hardcoded in views: all colors/radii/spacings come from here.
pragma Singleton
import QtQuick

QtObject {
    // Palette
    readonly property color background: "#10110F"       // restrained app chrome
    readonly property color canvas: "#0A0B0A"            // true black work surface
    readonly property color panel: "#151613"             // functional rail surface
    readonly property color heroPanel: "#11120F"         // elevated overlay surface
    readonly property color panelAlt: "#1B1C18"          // selected/inset surface
    readonly property color well: "#0D0E0C"              // video and stage well
    readonly property color border: Qt.rgba(245, 241, 236, 0.07)
    readonly property color hairline: Qt.rgba(245, 241, 236, 0.12)
    readonly property color accent: "#D97757"             // terracotta / A
    readonly property color accentPressed: "#B95D42"
    readonly property color accentMuted: Qt.rgba(0.85, 0.47, 0.34, 0.12)
    readonly property color sage: "#7C9885"              // sage / B
    readonly property color sageBright: "#A0B9A9"
    readonly property color sageMuted: Qt.rgba(0.49, 0.60, 0.52, 0.11)
    readonly property color glowA: Qt.rgba(0.85, 0.47, 0.34, 0.18)
    readonly property color glowB: Qt.rgba(0.49, 0.60, 0.52, 0.14)
    readonly property color textPrimary: "#F5F1EC"
    readonly property color textSecondary: "#B0AEA6"
    readonly property color textDisabled: "#77766F"

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
    readonly property int sidePanelWidth: 296
    readonly property int resultRowHeight: 32 // virtualized rows
    readonly property int margins: 12
}
