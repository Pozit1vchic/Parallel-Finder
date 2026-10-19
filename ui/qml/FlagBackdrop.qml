import QtQuick
import PfUi

// Wide, single-emblem flag compositions are built for the language-card ratio.
// The small canvas is only used as a paint-reveal mask; it never stretches a
// square flag or redraws multiple tiled copies.
Item {
    id: root
    property string country: "ru"
    property bool active: false
    property real reveal: active ? 1 : 0
    property real imageScale: active ? 1.035 : 1.0
    readonly property string bannerCode: ({ ru: "ru", en: "uk", ky: "ky", kk: "kk", uk: "ukr", tr: "tr" })[country] || "ru"
    readonly property string bannerSource: "qrc:/qt/qml/PfUi/qml/assets/flags/banner-" + root.bannerCode + ".svg"
    property bool imageReady: false
    readonly property var paintStyle: ({
        ru: { direction: "down", amplitude: 0.075, waves: 2.1, phase: 0.2, duration: 660, ease: Easing.OutCubic },
        en: { direction: "left", amplitude: 0.095, waves: 2.8, phase: 1.7, duration: 760, ease: Easing.OutQuart },
        ky: { direction: "right", amplitude: 0.08, waves: 2.3, phase: 3.1, duration: 700, ease: Easing.OutCubic },
        kk: { direction: "down", amplitude: 0.11, waves: 3.2, phase: 4.6, duration: 820, ease: Easing.OutQuart },
        uk: { direction: "up", amplitude: 0.07, waves: 1.9, phase: 2.4, duration: 640, ease: Easing.OutCubic },
        tr: { direction: "right", amplitude: 0.09, waves: 2.6, phase: 5.2, duration: 730, ease: Easing.OutQuart }
    })[country] || ({ direction: "right", amplitude: 0.08, waves: 2.5, phase: 0.8, duration: 700, ease: Easing.OutCubic })
    clip: true

    Behavior on reveal {
        enabled: !Theme.reducedMotion
        NumberAnimation {
            duration: root.active ? root.paintStyle.duration : 190
            easing.type: root.active ? root.paintStyle.ease : Easing.InCubic
        }
    }
    Behavior on imageScale {
        enabled: !Theme.reducedMotion
        NumberAnimation { duration: root.active ? root.paintStyle.duration : 220; easing.type: root.active ? root.paintStyle.ease : Easing.InCubic }
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
            function onImageScaleChanged() { paint.requestPaint() }
        }

        Component.onCompleted: loadBanner()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (!available || width <= 0 || height <= 0 || !isImageLoaded(loadedSource)) return
            const progress = Math.max(0, Math.min(1, root.reveal))
            if (progress <= 0) return

            const style = root.paintStyle
            const wave = function(t) {
                return (Math.sin(t * Math.PI * 2 * style.waves + style.phase + progress * 2.0)
                    + 0.28 * Math.sin(t * Math.PI * 2 * (style.waves * 1.7) - style.phase))
                    * style.amplitude * Math.sin(Math.PI * progress)
            }
            const edge = function(t) {
                if (progress >= 0.999)
                    return style.direction === "up" || style.direction === "right" ? 0 : (style.direction === "down" ? height : width)
                if (style.direction === "down") return height * (progress + wave(t))
                if (style.direction === "up") return height * (1 - progress + wave(t))
                if (style.direction === "left") return width * (progress + wave(t))
                return width * (1 - progress + wave(t))
            }
            const radius = Math.min(Theme.radiusCard, width / 2, height / 2)
            ctx.save()
            ctx.beginPath()
            ctx.moveTo(radius, 0)
            ctx.lineTo(width - radius, 0); ctx.arcTo(width, 0, width, radius, radius)
            ctx.lineTo(width, height - radius); ctx.arcTo(width, height, width - radius, height, radius)
            ctx.lineTo(radius, height); ctx.arcTo(0, height, 0, height - radius, radius)
            ctx.lineTo(0, radius); ctx.arcTo(0, 0, radius, 0, radius)
            ctx.closePath(); ctx.clip()

            ctx.beginPath()
            const step = Math.max(2, Math.min(width, height) / 13)
            if (style.direction === "down") {
                ctx.moveTo(0, 0); ctx.lineTo(width, 0)
                for (let x = width; x >= 0; x -= step) ctx.lineTo(x, edge(x / width))
                ctx.lineTo(0, 0)
            } else if (style.direction === "up") {
                ctx.moveTo(0, height); ctx.lineTo(width, height)
                for (let x = width; x >= 0; x -= step) ctx.lineTo(x, edge(x / width))
                ctx.lineTo(0, height)
            } else if (style.direction === "left") {
                ctx.moveTo(width, 0)
                for (let y = 0; y <= height; y += step) ctx.lineTo(edge(y / height), y)
                ctx.lineTo(width, height)
            } else {
                ctx.moveTo(0, 0)
                for (let y = 0; y <= height; y += step) ctx.lineTo(edge(y / height), y)
                ctx.lineTo(0, height)
            }
            ctx.closePath(); ctx.clip()

            const sourceWidth = 600
            const sourceHeight = 102
            const scale = Math.max(width / sourceWidth, height / sourceHeight) * root.imageScale
            const imageWidth = sourceWidth * scale
            const imageHeight = sourceHeight * scale
            ctx.globalAlpha = 1
            ctx.drawImage(loadedSource, (width - imageWidth) / 2, (height - imageHeight) / 2, imageWidth, imageHeight)
            ctx.restore()
        }
    }
}
