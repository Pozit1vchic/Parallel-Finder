// Parallel-Finder design tokens — Claude/Anthropic style (spec section 5).
// Nothing is hardcoded in views: all colors/radii/spacings come from here.
pragma Singleton
import QtQuick

QtObject {
    // Palette
    readonly property color background: "#0C0D0B"       // true black work surface
    readonly property color canvas: "#0C0D0B"
    readonly property color rail: "#11120F"
    property real surfaceOpacity: 1.0
    property color panel: Qt.rgba(0.09, 0.094, 0.075, surfaceOpacity)
    readonly property color heroPanel: panel
    property color panelAlt: Qt.rgba(0.118, 0.122, 0.098, surfaceOpacity)
    readonly property color surfaceRaised: panelAlt
    readonly property color surfaceMuted: "#272820"
    readonly property color well: "#0A0B09"
    readonly property color border: Qt.rgba(245, 241, 236, 0.06)
    readonly property color hairline: Qt.rgba(245, 241, 236, 0.10)
    readonly property color hairlineStrong: Qt.rgba(245, 241, 236, 0.18)
    property color accent: "#D97757"             // terracotta / A
    readonly property color accentBright: Qt.lighter(accent, 1.12)
    readonly property color accentPressed: Qt.darker(accent, 1.25)
    readonly property color accentMuted: Qt.rgba(accent.r, accent.g, accent.b, 0.12)
    property color sage: "#7C9885"              // sage / B
    readonly property color sageBright: Qt.lighter(sage, 1.18)
    readonly property color sagePressed: Qt.darker(sage, 1.25)
    readonly property color sageMuted: Qt.rgba(sage.r, sage.g, sage.b, 0.11)
    readonly property color glowA: Qt.rgba(accent.r, accent.g, accent.b, 0.18)
    readonly property color glowB: Qt.rgba(sage.r, sage.g, sage.b, 0.14)
    readonly property color textPrimary: "#F5F1EC"
    readonly property color textSecondary: "#ACA89F"
    readonly property color textDisabled: "#77766F"

    // Typography
    property string fontFamily: "Segoe UI"
    readonly property string displayFont: "Georgia"
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeBody: 13
    readonly property int fontSizeTitle: 18

    // Radii: buttons 8-10, cards 10-12, splash 16
    property real radiusScale: 1.0
    readonly property int radiusButton: Math.round(8 * radiusScale)
    readonly property int radiusCard: Math.round(12 * radiusScale)
    readonly property int radiusSplash: Math.round(16 * radiusScale)
    readonly property int radiusOverlay: Math.round(16 * radiusScale)

    // Shadows: blur 14-24, opacity 0.08-0.35
    readonly property int shadowBlurPanel: 16
    readonly property int shadowBlurOverlay: 24
    readonly property real shadowOpacity: 0.12
    readonly property color shadowPanel: Qt.rgba(0, 0, 0, 0.48)
    readonly property color shadowOverlay: Qt.rgba(0, 0, 0, 0.82)
    readonly property color overlayDim: Qt.rgba(0, 0, 0, 0.74)

    // Icon line width (Lucide, line-style)
    readonly property real iconStrokeWidth: 1.6

    // Layout
    readonly property int topBarHeight: 68
    readonly property int sidePanelWidth: 286
    readonly property int resultRowHeight: 32 // virtualized rows
    readonly property int margins: 12
    property bool reducedMotion: false
    readonly property int motionDuration: reducedMotion ? 0 : 160
}
