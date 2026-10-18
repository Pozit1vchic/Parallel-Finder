// Editing workspace tokens: quiet controls, one primary action, numeric type.
pragma Singleton
import QtQuick

QtObject {
    // Palette
    property bool darkMode: true
    readonly property color background: darkMode ? "#0C0D10" : "#F2F3F5"
    readonly property color canvas: background
    readonly property color rail: darkMode ? "#141418" : "#E8EAEE"
    property real surfaceOpacity: 1.0
    // The slider is intentionally perceptual: dark surfaces otherwise look
    // almost transparent even when the value says 70–80%. Keep 25% as a
    // genuinely light surface, while the middle of the range remains legible.
    readonly property real surfaceAlpha: {
        const normalized = Math.max(0, Math.min(1, (surfaceOpacity - 0.25) / 0.75))
        return 0.30 + 0.70 * Math.pow(normalized, 0.65)
    }
    property color panel: darkMode ? Qt.rgba(0.071, 0.071, 0.086, surfaceAlpha) : Qt.rgba(1, 1, 1, surfaceAlpha)
    readonly property color heroPanel: panel
    property color panelAlt: darkMode ? Qt.rgba(0.110, 0.110, 0.133, surfaceAlpha) : Qt.rgba(0.91, 0.92, 0.94, surfaceAlpha)
    readonly property color surfaceRaised: panelAlt
    readonly property color surfaceMuted: darkMode ? "#27272E" : "#DDE0E5"
    readonly property color well: darkMode ? "#101014" : "#FFFFFF"
    readonly property color border: darkMode ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0.08, 0.10, 0.14, 0.10)
    readonly property color hairline: darkMode ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0.08, 0.10, 0.14, 0.13)
    readonly property color hairlineStrong: darkMode ? "#3F3F46" : "#B8BDC6"
    property color accent: "#D97757"
    // The global accent normally uses one hue on every result.  The second
    // stop is reserved for an optional two-tone palette and the B preview.
    property color accentSecondary: accent
    property bool accentGradientEnabled: false
    readonly property color accentBright: Qt.lighter(accent, 1.12)
    readonly property color accentPressed: Qt.darker(accent, 1.25)
    readonly property color accentMuted: Qt.rgba(accent.r, accent.g, accent.b, 0.12)
    // Keep the old token names as aliases so every QML component follows the
    // user's global theme instead of introducing a fixed green for side B.
    readonly property color sage: accentSecondary
    readonly property color sageBright: Qt.lighter(sage, 1.18)
    readonly property color sagePressed: Qt.darker(sage, 1.25)
    readonly property color sageMuted: Qt.rgba(sage.r, sage.g, sage.b, 0.11)
    readonly property color accentForeground: {
        const r = accentGradientEnabled ? (accent.r + accentSecondary.r) / 2 : accent.r
        const g = accentGradientEnabled ? (accent.g + accentSecondary.g) / 2 : accent.g
        const b = accentGradientEnabled ? (accent.b + accentSecondary.b) / 2 : accent.b
        return 0.2126 * r + 0.7152 * g + 0.0722 * b > 0.56 ? background : textPrimary
    }
    readonly property color textPrimary: darkMode ? "#F5F1EC" : "#191B20"
    readonly property color textSecondary: darkMode ? "#A1A1AA" : "#555B66"
    readonly property color textDisabled: darkMode ? "#71717A" : "#7B818B"

    // Typography
    property string fontFamily: "Segoe UI Variable"
    readonly property string displayFont: fontFamily
    readonly property string monoFont: numericFont.name
    property FontLoader numericFont: FontLoader { source: "fonts/JetBrainsMono-Regular.ttf" }
    property bool reducedMotion: false
    readonly property int motionDuration: reducedMotion ? 0 : 120
    readonly property int motionChangeDuration: reducedMotion ? 0 : 180
    readonly property int motionRevealDuration: reducedMotion ? 0 : 420
    readonly property int motionPopupDuration: reducedMotion ? 0 : 220
    readonly property int motionCardDuration: reducedMotion ? 0 : 320
    readonly property int motionPopupOffset: reducedMotion ? 0 : 8
    readonly property real motionPopupScale: reducedMotion ? 1 : 0.965
    readonly property real motionCardScale: reducedMotion ? 1 : 0.986
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
