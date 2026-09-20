// Editing workspace tokens: quiet controls, one primary action, numeric type.
pragma Singleton
import QtQuick

QtObject {
    // Palette
    readonly property color background: "#0C0D10"
    readonly property color canvas: background
    readonly property color rail: "#141418"
    property real surfaceOpacity: 1.0
    // The slider is intentionally perceptual: dark surfaces otherwise look
    // almost transparent even when the value says 70–80%. Keep 25% as a
    // genuinely light surface, while the middle of the range remains legible.
    readonly property real surfaceAlpha: {
        const normalized = Math.max(0, Math.min(1, (surfaceOpacity - 0.25) / 0.75))
        return 0.30 + 0.70 * Math.pow(normalized, 0.65)
    }
    property color panel: Qt.rgba(0.071, 0.071, 0.086, surfaceAlpha)
    readonly property color heroPanel: panel
    property color panelAlt: Qt.rgba(0.110, 0.110, 0.133, surfaceAlpha)
    readonly property color surfaceRaised: panelAlt
    readonly property color surfaceMuted: "#27272E"
    readonly property color well: "#101014"
    readonly property color border: Qt.rgba(1, 1, 1, 0.06)
    readonly property color hairline: Qt.rgba(1, 1, 1, 0.10)
    readonly property color hairlineStrong: "#3F3F46"
    property color accent: "#D97757"             // terracotta / A
    readonly property color accentBright: Qt.lighter(accent, 1.12)
    readonly property color accentPressed: Qt.darker(accent, 1.25)
    readonly property color accentMuted: Qt.rgba(accent.r, accent.g, accent.b, 0.12)
    property color sage: "#7C9885"              // sage / B
    readonly property color sageBright: Qt.lighter(sage, 1.18)
    readonly property color sagePressed: Qt.darker(sage, 1.25)
    readonly property color sageMuted: Qt.rgba(sage.r, sage.g, sage.b, 0.11)
    readonly property color textPrimary: "#F5F1EC"
    readonly property color textSecondary: "#A1A1AA"
    readonly property color textDisabled: "#71717A"

    // Typography
    property string fontFamily: "Segoe UI Variable"
    readonly property string displayFont: fontFamily
    readonly property string monoFont: numericFont.name
    property FontLoader numericFont: FontLoader { source: "fonts/JetBrainsMono-Regular.ttf" }
    property bool reducedMotion: false
    readonly property int motionDuration: reducedMotion ? 0 : 120
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeBody: 13
    readonly property int fontSizeTitle: 18

    // Radii: buttons 8-10, cards 10-12, overlays 16
    readonly property int radiusButton: 8
    readonly property int radiusCard: 12
    readonly property int radiusOverlay: 16

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
}
