import QtQuick
import PfUi

// One paint-like reveal shared by every language. The banner is stretched to
// the exact button bounds so the full card is always covered (no tiling/crop).
Item {
    id: root
    property string country: "ru"
    property bool active: false
    property real reveal: active ? 1 : 0
    property bool imageReady: false
    readonly property string bannerCode: ({ ru: "ru", en: "uk", ky: "ky", kk: "kk", uk: "ukr", tr: "tr" })[country] || "ru"
    readonly property string bannerSource: "qrc:/qt/qml/PfUi/qml/assets/flags/banner-" + root.bannerCode + ".svg"
    clip: true

    Behavior on reveal {
        enabled: !Theme.reducedMotion
        NumberAnimation {
            duration: root.active ? 650 : 180
            easing.type: root.active ? Easing.OutCubic : Easing.InCubic
        }
    }

    Canvas {
        id: paint
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        property string loadedSource: ""
        onAvailableChanged: if (available) loadBanner()
        onImageLoaded: {
            if (isImageLoaded(root.bannerSource)) root.imageReady = true
            requestPaint()
        }

        function loadBanner() {
            if (loadedSource !== root.bannerSource) {
                root.imageReady = false
                loadedSource = root.bannerSource
                loadImage(loadedSource)
            }
            requestPaint()
        }

        Connections {
            target: root
            function onBannerSourceChanged() { paint.loadBanner() }
            function onRevealChanged() { paint.requestPaint() }
        }

        Component.onCompleted: loadBanner()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (!available || width <= 0 || height <= 0 || !isImageLoaded(loadedSource)) return

            const progress = Math.max(0, Math.min(1, root.reveal))
            if (progress <= 0) return
            const radius = Math.min(Theme.radiusCard, width / 2, height / 2)

            ctx.save()
            // Keep the flag inside the rounded button at every animation frame.
            ctx.beginPath()
            ctx.moveTo(radius, 0)
            ctx.lineTo(width - radius, 0); ctx.arcTo(width, 0, width, radius, radius)
            ctx.lineTo(width, height - radius); ctx.arcTo(width, height, width - radius, height, radius)
            ctx.lineTo(radius, height); ctx.arcTo(0, height, 0, height - radius, radius)
            ctx.lineTo(0, radius); ctx.arcTo(0, 0, radius, 0, radius)
            ctx.closePath(); ctx.clip()

            // A single gently irregular edge travels left to right for all flags.
            ctx.beginPath()
            ctx.moveTo(0, 0)
            const amplitude = width * 0.035 * Math.sin(Math.PI * progress)
            const edgeAt = function(y) {
                const t = y / height
                return Math.max(0, Math.min(width, width * progress
                    + amplitude * (Math.sin(t * Math.PI * 4 + progress * 2.2)
                        + 0.24 * Math.sin(t * Math.PI * 7 - 0.7))))
            }
            const step = Math.max(2, height / 14)
            for (let y = 0; y <= height; y += step) ctx.lineTo(edgeAt(y), y)
            ctx.lineTo(0, height)
            ctx.closePath(); ctx.clip()

            // Fill the whole button surface uniformly; no empty bands or repeats.
            ctx.globalAlpha = 1
            ctx.drawImage(loadedSource, 0, 0, width, height)
            ctx.restore()
        }
    }
}
