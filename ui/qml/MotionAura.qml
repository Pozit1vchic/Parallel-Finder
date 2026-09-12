import QtQuick
import PfUi

Item {
    id: root
    property real targetX: 0.5
    property real targetY: 0.5
    property bool active: true
    property real phase: 0
    width: 430
    height: 300
    opacity: active ? 1 : 0

    Behavior on x { NumberAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    Behavior on y { NumberAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }

    Timer {
        interval: 24
        running: root.active && !Theme.reducedMotion
        repeat: true
        onTriggered: { root.phase += 0.045; particles.requestPaint() }
    }
    Canvas {
        id: particles
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const cx = width * (0.50 + (root.targetX - 0.5) * 0.16)
            const cy = height * (0.50 + (root.targetY - 0.5) * 0.16)
            const maxRadius = Math.min(width, height) * 0.46
            for (let i = 0; i < 176; ++i) {
                const arm = i % 7
                const ring = Math.floor(i / 7) / 25.2
                const angle = arm * 0.897 + ring * 5.45 + Math.sin(root.phase * 0.55 + i * 0.19) * 0.075
                const radius = maxRadius * (0.18 + ring * 0.82)
                const wobble = Math.sin(root.phase * 0.8 + i * 1.37) * (2.5 + ring * 4.0)
                const px = cx + Math.cos(angle) * (radius + wobble)
                const py = cy + Math.sin(angle) * (radius * 0.62 + wobble * 0.4)
                const falloff = Math.max(0, 1 - ring)
                const alpha = 0.10 + falloff * 0.50
                const size = 0.7 + falloff * (i % 5 === 0 ? 2.8 : 1.45)
                const useSage = (i + arm) % 5 === 0
                const color = useSage ? Theme.sage : Theme.accent
                if (i % 9 === 0) {
                    const glow = ctx.createRadialGradient(px, py, 0, px, py, size * 4.8)
                    glow.addColorStop(0, Qt.rgba(color.r, color.g, color.b, alpha * 0.45))
                    glow.addColorStop(1, Qt.rgba(color.r, color.g, color.b, 0))
                    ctx.fillStyle = glow
                    ctx.fillRect(px - size * 5, py - size * 5, size * 10, size * 10)
                }
                ctx.globalAlpha = alpha
                ctx.fillStyle = color
                ctx.beginPath()
                ctx.arc(px, py, size, 0, Math.PI * 2)
                ctx.fill()
            }
            ctx.globalAlpha = 1
        }
        Component.onCompleted: requestPaint()
    }
}
