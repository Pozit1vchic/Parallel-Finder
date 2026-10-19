import QtQuick
import PfUi

// One painted flag reveal, clipped by an irregular brush edge. Each flag has
// its own flow instead of revealing through a stack of rectangular strips.
Item {
    id: root
    property string country: "ru"
    property bool active: false
    property real reveal: active ? 1 : 0
    readonly property var paintStyle: ({
        ru: { amplitude: 0.055, waves: 2.1, phase: 0.2, duration: 700, ease: Easing.OutCubic },
        en: { amplitude: 0.085, waves: 3.4, phase: 1.7, duration: 820, ease: Easing.OutQuart },
        ky: { amplitude: 0.065, waves: 2.7, phase: 3.1, duration: 760, ease: Easing.OutBack },
        kk: { amplitude: 0.095, waves: 3.8, phase: 4.6, duration: 900, ease: Easing.OutCubic },
        uk: { amplitude: 0.045, waves: 1.6, phase: 2.4, duration: 680, ease: Easing.OutQuart },
        tr: { amplitude: 0.075, waves: 3.0, phase: 5.2, duration: 780, ease: Easing.OutBack }
    })[country] || ({ amplitude: 0.06, waves: 2.4, phase: 0.8, duration: 740, ease: Easing.OutCubic })
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
            if (!available || width <= 0 || height <= 0 || !isImageLoaded(flagSource)) return
            const p = Math.max(0, Math.min(1, root.reveal))
            if (p <= 0) return

            const style = root.paintStyle
            const edgeAt = function(y, offset) {
                const t = y / Math.max(1, height)
                const envelope = Math.sin(Math.PI * Math.max(0, Math.min(1, p)))
                const wave = Math.sin(t * Math.PI * 2 * style.waves + style.phase + p * 2.2)
                    + 0.32 * Math.sin(t * Math.PI * 2 * (style.waves * 1.83) - style.phase)
                return width * (p + offset + wave * style.amplitude * envelope)
            }
            const makeEdgePath = function(offset) {
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(edgeAt(0, offset), 0)
                const step = Math.max(3, height / 34)
                for (let y = step; y < height; y += step)
                    ctx.lineTo(edgeAt(y, offset), y)
                ctx.lineTo(edgeAt(height, offset), height)
                ctx.lineTo(0, height)
                ctx.closePath()
            }

            // A few translucent brush wisps lead the opaque pigment edge;
            // they share the flag image, so this reads as paint, not tiles.
            ctx.save()
            makeEdgePath(0.055)
            ctx.clip()
            ctx.globalAlpha = 0.22
            ctx.drawImage(flagSource, 0, 0, width, height)
            ctx.restore()

            ctx.save()
            makeEdgePath(-0.012)
            ctx.clip()
            ctx.drawImage(flagSource, 0, 0, width, height)
            ctx.restore()
        }
    }
}
