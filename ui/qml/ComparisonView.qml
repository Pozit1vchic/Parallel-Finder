import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import PfUi
import PfUiBridge

// Still frame and bounded inline playback share the same A/B viewport.
Item {
    id: root
    property var record: null
    property string side: "left"
    property color accentColor: Theme.accent
    property string title: side === "left" ? "A" : "B"
    property string previewSource: record ? (side === "left" ? String(record.leftPreview || "") : String(record.rightPreview || "")) : ""
    property string sourcePath: record ? (side === "left" ? String(record.leftSource || "") : String(record.rightSource || "")) : ""
    property real timestamp: record ? (side === "left" ? Number(record.leftStart || 0) : Number(record.rightStart || 0)) : 0
    property real previewStart: record ? (side === "left" ? Number(record.leftSceneStart !== undefined ? record.leftSceneStart : record.leftStart || 0) : Number(record.rightSceneStart !== undefined ? record.rightSceneStart : record.rightStart || 0)) : 0
    property real previewEnd: record ? (side === "left" ? Number(record.leftSceneEnd !== undefined ? record.leftSceneEnd : record.leftEnd || 0) : Number(record.rightSceneEnd !== undefined ? record.rightSceneEnd : record.rightEnd || 0)) : 0
    property real matchEnd: record ? Number(side === "left" ? record.leftEnd || 0 : record.rightEnd || 0) : 0
    property real playbackEnd: !record ? 0 : record.matchType === "motion" && matchEnd > timestamp
        ? matchEnd : (previewEnd > timestamp ? Math.min(previewEnd, timestamp + 4) : timestamp + 4)
    readonly property bool previewPlayable: !!record && sourcePath.length > 0 && playbackEnd > timestamp
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    signal frameUnavailable(string side)
    signal previewRequested()
    signal playbackFinished()
    signal playbackFailed(string message)
    property bool videoMode: false
    property bool playbackReady: false
    property bool pendingSinglePlayback: false
    readonly property var player: playerLoader.item
    readonly property bool playing: !!player && player.playbackState === MediaPlayer.PlayingState

    onRecordChanged: stopPlayback()
    function preparePlayback() {
        stopPlayback()
        if (!root.previewPlayable || !player) return
        videoMode = true
        player.source = Analysis.videoSourceUrl(root.sourcePath)
    }
    function playPrepared() { if (player) player.play() }
    function pausePlayback() { if (player) player.pause() }
    function togglePlayback() {
        if (playing) pausePlayback()
        else if (videoMode && playbackReady) playPrepared()
        else {
            preparePlayback()
            pendingSinglePlayback = videoMode
            if (pendingSinglePlayback && playbackReady) {
                pendingSinglePlayback = false
                playPrepared()
            }
        }
    }
    onPlaybackReadyChanged: {
        if (playbackReady && pendingSinglePlayback) {
            pendingSinglePlayback = false
            playPrepared()
        }
    }
    function stopPlayback() {
        pendingSinglePlayback = false
        playbackReady = false
        if (player) {
            player.stop()
            player.source = ""
        }
        videoMode = false
    }
    Loader {
        id: playerLoader
        active: !!root.record
        sourceComponent: Component {
            MediaPlayer {
                objectName: "inlineMediaPlayer"
                videoOutput: videoLoader.item
                audioOutput: AudioOutput { muted: true }
                onMediaStatusChanged: {
                    if (mediaStatus === MediaPlayer.LoadedMedia) {
                        position = Math.round(root.timestamp * 1000)
                        root.playbackReady = true
                    }
                    if (mediaStatus === MediaPlayer.EndOfMedia) root.playbackFinished()
                }
                onErrorOccurred: function(error, errorString) { root.playbackFailed(errorString); root.stopPlayback() }
            }
        }
    }
    Timer {
        interval: 25; repeat: true; running: root.playing
        onTriggered: {
            if (player && player.position >= root.playbackEnd * 1000) root.playbackFinished()
        }
    }

    function timecode(seconds) {
        const total = Math.max(0, Number(seconds) || 0)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = Math.floor(total % 60)
        return [h, m, s].map(function (v) { return String(v).padStart(2, "0") }).join(":")
    }

    function fileName(path) {
        const parts = String(path || "").replace(/\\/g, "/").split("/")
        return parts[parts.length - 1] || L10n.t("common.empty")
    }

    function resetView() {
        root.zoom = 1.0
        root.panX = 0
        root.panY = 0
    }

    function setZoom(nextZoom) {
        root.zoom = Math.max(1.0, Math.min(4.0, nextZoom))
        if (root.zoom <= 1.001) {
            root.zoom = 1.0
            root.panX = 0
            root.panY = 0
        } else {
            clampPan()
        }
    }

    function clampPan() {
        const maxX = Math.max(0, (frameViewport.width * root.zoom - frameViewport.width) / 2)
        const maxY = Math.max(0, (frameViewport.height * root.zoom - frameViewport.height) / 2)
        root.panX = Math.max(-maxX, Math.min(maxX, root.panX))
        root.panY = Math.max(-maxY, Math.min(maxY, root.panY))
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.canvas
        radius: Theme.radiusButton
        border.color: root.accentColor
        border.width: 1

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Row {
                width: parent.width
                height: 24
                Text { text: root.title; color: root.accentColor; font.family: Theme.displayFont; font.pixelSize: 16; verticalAlignment: Text.AlignVCenter }
                Item { width: parent.width - timeText.implicitWidth - resetButton.width - 16; height: 1 }
                Text { font.family: Theme.fontFamily; id: timeText; text: root.timecode(root.timestamp); color: Theme.textSecondary; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter }
                PfIconButton {
                    id: resetButton
                    width: 24
                    height: 24
                    visible: root.zoom > 1.001
                    iconSource: "qrc:/qt/qml/PfUi/qml/assets/maximize.svg"
                    accessibleName: L10n.t("comparison.fit")
                    onClicked: root.resetView()
                }
            }

            Item {
                id: frameViewport
                width: parent.width
                height: Math.max(80, parent.height - 104)
                clip: true

                Rectangle { anchors.fill: parent; color: Theme.well; radius: 6; border.color: Theme.hairline }

                Item {
                    id: frameLayer
                    x: (frameViewport.width - width) / 2 + root.panX
                    y: (frameViewport.height - height) / 2 + root.panY
                    width: frameViewport.width * root.zoom
                    height: frameViewport.height * root.zoom

                    Image {
                        id: frameImage
                        anchors.fill: parent
                        anchors.margins: 6 * root.zoom
                        source: root.previewSource
                        asynchronous: true
                        cache: false
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        visible: status === Image.Ready && !root.videoMode
                        onStatusChanged: if (status === Image.Error) root.frameUnavailable(root.side)
                    }
                    Loader {
                        id: videoLoader
                        anchors.fill: parent
                        anchors.margins: 6 * root.zoom
                        active: !!root.record
                        sourceComponent: Component {
                            VideoOutput {
                                objectName: "inlineVideoOutput"
                                fillMode: VideoOutput.PreserveAspectFit
                                visible: root.videoMode
                            }
                        }
                    }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    visible: !root.videoMode && frameImage.status !== Image.Ready
                    Image { anchors.horizontalCenter: parent.horizontalCenter; source: "qrc:/qt/qml/PfUi/qml/assets/film.svg"; sourceSize.width: 24; sourceSize.height: 24; opacity: 0.72 }
                    Text { font.family: Theme.fontFamily; anchors.horizontalCenter: parent.horizontalCenter; text: frameImage.status === Image.Loading ? L10n.t("timeline.loadingFrame") : L10n.t("timeline.frameUnavailable"); color: Theme.textSecondary; font.pixelSize: 11 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.timecode(root.timestamp); color: root.accentColor; font.family: Theme.displayFont; font.pixelSize: 16 }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: frameImage.status === Image.Ready
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    property real pressX: 0
                    property real pressY: 0
                    property real originX: 0
                    property real originY: 0
                    onPressed: {
                        pressX = mouse.x
                        pressY = mouse.y
                        originX = root.panX
                        originY = root.panY
                    }
                    onPositionChanged: if (pressed && root.zoom > 1.001) {
                        root.panX = originX + mouse.x - pressX
                        root.panY = originY + mouse.y - pressY
                        root.clampPan()
                    }
                    onDoubleClicked: root.resetView()
                    onWheel: function(wheel) {
                        root.setZoom(root.zoom + (wheel.angleDelta.y > 0 ? 0.2 : -0.2))
                        wheel.accepted = true
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 6

                Text {
                    font.family: Theme.fontFamily
                    width: parent.width
                    text: root.fileName(root.sourcePath)
                    color: Theme.textDisabled
                    font.pixelSize: 9
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                }

                PfButton {
                    objectName: "previewPlayButton"
                    visible: !!root.record
                    width: Math.min(parent.width, 154)
                    text: root.playing ? L10n.t("preview.pause") : L10n.t("preview.play")
                    quiet: true
                    enabled: root.previewPlayable && (!root.videoMode || root.playbackReady)
                    onClicked: root.previewRequested()
                }
            }
        }
    }
}
