import QtQuick
import PfUi

// A flag-colour wash reveals with one organic brush edge. The small flag mark
// keeps its native 2:1 proportions; it is never stretched across the card.
Item {
    id: root
    property string country: "ru"
    property bool active: false
    property real reveal: active ? 1 : 0
    readonly property var paintStyle: ({
        ru: { direction: "down", amplitude: 0.075, waves: 2.2, phase: 0.2, duration: 720, ease: Easing.OutCubic, colors: ["#F7F7F5", "#4169A8", "#D84B55"] },
        en: { direction: "up", amplitude: 0.11, waves: 3.4, phase: 1.7, duration: 840, ease: Easing.OutQuart, colors: ["#243B70", "#F4F1EC", "#C84A52"] },
        ky: { direction: "right", amplitude: 0.085, waves: 2.7, phase: 3.1, duration: 780, ease: Easing.OutCubic, colors: ["#D9474F", "#F1D5D4", "#C42E3A"] },
        kk: { direction: "down", amplitude: 0.12, waves: 3.8, phase: 4.6, duration: 900, ease: Easing.OutQuart, colors: ["#40A8C7", "#F4D34F", "#3C9C76"] },
        uk: { direction: "up", amplitude: 0.07, waves: 1.8, phase: 2.4, duration: 700, ease: Easing.OutCubic, colors: ["#356BB0", "#F5D25D", "#E8C94B"] },
        tr: { direction: "right", amplitude: 0.095, waves: 3.0, phase: 5.2, duration: 800, ease: Easing.OutQuart, colors: ["#D6424B", "#F0DADD", "#C72E3C"] }
    })[country] || ({ direction: "right", amplitude: 0.08, waves: 2.5, phase: 0.8, duration: 740, ease: Easing.OutCubic, colors: ["#6E79A8", "#EEE8E0", "#C76A63"] })
    clip: true

    Behavior on reveal {
        enabled: !Theme.reducedMotion
        NumberAnimation {
            duration: root.active ? root.paintStyle.duration : 240
            easing.type: root.active ? root.paintStyle.ease : Easing.InCubic
        }
    }

    Canvas {
        id: pigment
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        property string flagSource: "qrc:/qt/qml/PfUi/qml/assets/flags/" + root.country + ".svg"
        onFlagSourceChanged: {
            if (!isImageLoaded(flagSource)) loadImage(flagSource)
            requestPaint()
        }
        onAvailableChanged: if (available && !isImageLoaded(flagSource)) loadImage(flagSource)
        onImageLoaded: requestPaint()
        Connections {
            target: root
            function onRevealChanged() { pigment.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (!available || width <= 0 || height <= 0) return
            const p = Math.max(0, Math.min(1, root.reveal))
            if (p <= 0) return

            const style = root.paintStyle
            const amp = style.amplitude
            const waveAt = function(t) {
                return (Math.sin(t * Math.PI * 2 * style.waves + style.phase + p * 2.2)
                    + 0.32 * Math.sin(t * Math.PI * 2 * (style.waves * 1.83) - style.phase))
                    * amp * Math.sin(Math.PI * p)
            }
            const boundary = function(t, offset) {
                if (p >= 0.999) return style.direction === "up" || style.direction === "right" ? 0 : (style.direction === "down" ? height : width)
                if (style.direction === "down") return height * (p + offset + waveAt(t))
                if (style.direction === "up") return height * (1 - p + offset + waveAt(t))
                if (style.direction === "left") return width * (p + offset + waveAt(t))
                return width * (1 - p + offset + waveAt(t))
            }
            const makeEdgePath = function(offset) {
                const step = Math.max(3, Math.min(width, height) / 15)
                ctx.beginPath()
                if (style.direction === "down") {
                    ctx.moveTo(0, 0); ctx.lineTo(width, 0)
                    for (let x = width; x >= 0; x -= step) ctx.lineTo(x, boundary(x / width, offset))
                    ctx.lineTo(0, 0)
                } else if (style.direction === "up") {
                    ctx.moveTo(0, height); ctx.lineTo(width, height)
                    for (let x = width; x >= 0; x -= step) ctx.lineTo(x, boundary(x / width, offset))
                    ctx.lineTo(0, height)
                } else if (style.direction === "left") {
                    ctx.moveTo(0, 0)
                    for (let y = 0; y <= height; y += step) ctx.lineTo(boundary(y / height, offset), y)
                    ctx.lineTo(0, height); ctx.closePath()
                } else {
                    ctx.moveTo(width, 0)
                    for (let y = 0; y <= height; y += step) ctx.lineTo(boundary(y / height, offset), y)
                    ctx.lineTo(width, height); ctx.closePath()
                }
                ctx.closePath()
            }
            const makeWash = function() {
                const horizontalSweep = style.direction === "left" || style.direction === "right"
                const gradient = horizontalSweep ? ctx.createLinearGradient(0, 0, 0, height)
                    : ctx.createLinearGradient(0, 0, width, 0)
                gradient.addColorStop(0, style.colors[0])
                gradient.addColorStop(0.5, style.colors[1])
                gradient.addColorStop(1, style.colors[2])
                ctx.fillStyle = gradient
                ctx.fillRect(0, 0, width, height)
            }

            // Match the card silhouette so the painted reveal reaches its
            // edges without exposing square corners beyond the rounded border.
            ctx.save()
            const radius = Math.min(Theme.radiusCard - 2, width / 2, height / 2)
            ctx.beginPath()
            ctx.moveTo(radius, 0)
            ctx.lineTo(width - radius, 0); ctx.arcTo(width, 0, width, radius, radius)
            ctx.lineTo(width, height - radius); ctx.arcTo(width, height, width - radius, height, radius)
            ctx.lineTo(radius, height); ctx.arcTo(0, height, 0, height - radius, radius)
            ctx.lineTo(0, radius); ctx.arcTo(0, 0, radius, 0, radius)
            ctx.closePath(); ctx.clip()

            // Soft pigment sits just ahead of the crisp, irregular edge.
            ctx.save()
            makeEdgePath(style.direction === "up" || style.direction === "right" ? -0.055 : 0.055)
            ctx.clip()
            ctx.globalAlpha = 0.22
            makeWash()
            ctx.restore()

            ctx.save()
            makeEdgePath(0)
            ctx.clip()
            ctx.globalAlpha = 0.58
            makeWash()
            if (isImageLoaded(flagSource)) {
                const flagWidth = Math.min(width * 0.48, height * 2)
                const flagHeight = flagWidth / 2
                ctx.globalAlpha = 0.68
                ctx.drawImage(flagSource, (width - flagWidth) / 2, (height - flagHeight) / 2, flagWidth, flagHeight)
            }
            ctx.restore()
            ctx.restore()
        }
    }
}
