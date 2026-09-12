import QtQuick
import PfUi

Item {
    id: root
    property real targetX: 0.5
    property real targetY: 0.5
    property bool active: true
    property real phase: 0
    property real breath: 0

    // Single source of truth for where the swarm is centered: one smoothed
    // position, updated once per frame in the same loop that repaints.
    // Previously the outer box was moved by a QML SpringAnimation reacting
    // to the raw mouse fraction, while the swarm's center *inside* that box
    // was offset again from the same raw value with zero smoothing -- two
    // separate systems fighting each other, which is what read as broken /
    // incoherent motion. Now there's exactly one.
    property real smoothX: 0.5
    property real smoothY: 0.5
    property real velX: 0
    property real velY: 0

    opacity: active ? 1 : 0

    function paintSprite(ctx, size, tint) {
        ctx.clearRect(0, 0, size, size)
        const r = size / 2
        const g = ctx.createRadialGradient(r, r, 0, r, r, r)
        g.addColorStop(0.0, Qt.rgba(tint.r, tint.g, tint.b, 0.95))
        g.addColorStop(0.15, Qt.rgba(tint.r, tint.g, tint.b, 0.55))
        g.addColorStop(0.4, Qt.rgba(tint.r, tint.g, tint.b, 0.18))
        g.addColorStop(1.0, Qt.rgba(tint.r, tint.g, tint.b, 0.0))
        ctx.fillStyle = g
        ctx.beginPath(); ctx.arc(r, r, r, 0, Math.PI * 2); ctx.fill()
    }

    Canvas {
        id: accentSprite
        visible: false
        width: 96; height: 96
        onPaint: root.paintSprite(getContext("2d"), width, Theme.accent)
        Component.onCompleted: requestPaint()
    }
    Canvas {
        id: sageSprite
        visible: false
        width: 96; height: 96
        onPaint: root.paintSprite(getContext("2d"), width, Theme.sage)
        Component.onCompleted: requestPaint()
    }

    Connections {
        target: Theme
        function onAccentChanged() { accentSprite.requestPaint() }
        function onSageChanged() { sageSprite.requestPaint() }
    }

    readonly property int particleCount: 200
    readonly property real ringsTotal: particleCount / 7

    Timer {
        interval: 16
        running: root.active && !Theme.reducedMotion
        repeat: true
        onTriggered: {
            // One explicit, tuned-once physics step: a gentle spring chase
            // toward the raw cursor position. Just underdamped enough to
            // carry a little momentum past the target before settling
            // (so tracking doesn't snap to a dead stop at the edge), but
            // not so underdamped that it visibly oscillates.
            const dt = 0.016
            const stiffness = 120
            const damping = 18
            const ax = (root.targetX - root.smoothX) * stiffness - root.velX * damping
            const ay = (root.targetY - root.smoothY) * stiffness - root.velY * damping
            root.velX += ax * dt
            root.velY += ay * dt
            root.smoothX += root.velX * dt
            root.smoothY += root.velY * dt

            root.phase += 0.03
            root.breath += 0.018
            particles.requestPaint()
        }
    }

    Canvas {
        id: particles
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")

            // Trail fade instead of a hard clear -- the previous frame dims
            // slightly rather than being wiped, leaving a soft comet tail
            // behind every moving particle (the "motion blur").
            ctx.globalCompositeOperation = "destination-out"
            ctx.fillStyle = "rgba(0, 0, 0, 0.16)"
            ctx.fillRect(0, 0, width, height)
            ctx.globalCompositeOperation = "source-over"

            const cx = width * root.smoothX
            const cy = height * root.smoothY

            const breathScale = 0.82 + Math.sin(root.breath) * 0.18
            const maxRadius = Math.min(width, height) * 0.42 * breathScale

            for (let i = 0; i < root.particleCount; ++i) {
                const arm = i % 7
                const ring = Math.floor(i / 7) / root.ringsTotal

                const flowA = Math.sin(root.phase * 0.55 + i * 0.19)
                const flowB = Math.cos(root.phase * 0.34 + i * 0.41)
                const angle = arm * 0.897 + ring * 5.45 + flowA * 0.09 + flowB * 0.05
                const radius = maxRadius * (0.18 + ring * 0.82)
                const wobble = flowA * (2.0 + ring * 3.0) + flowB * (1.2 + ring * 1.6)

                const px = cx + Math.cos(angle) * (radius + wobble)
                const py = cy + Math.sin(angle) * (radius * 0.62 + wobble * 0.4)

                const falloff = Math.max(0, 1 - ring)
                const twinkle = 0.7 + 0.3 * Math.sin(root.phase * 1.1 + i * 1.7)
                const alpha = (0.12 + falloff * 0.55) * twinkle
                const size = (0.7 + falloff * (i % 5 === 0 ? 2.8 : 1.45)) * (0.92 + breathScale * 0.12)

                const useSage = (i + arm) % 5 === 0
                const sprite = useSage ? sageSprite : accentSprite
                const glowSize = size * (i % 5 === 0 ? 7.5 : 5.0)

                ctx.globalAlpha = alpha
                ctx.drawImage(sprite, px - glowSize, py - glowSize, glowSize * 2, glowSize * 2)
            }
            ctx.globalAlpha = 1
        }
        Component.onCompleted: requestPaint()
    }
}
