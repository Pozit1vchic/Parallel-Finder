import QtQuick
import QtQuick.Controls.Basic
import PfUi

// A deterministic stop-frame surface. The analysis pipeline already decodes
// the exact timestamps with FFmpeg, so this view intentionally does not create
// a second QMediaPlayer/QVideoSink pipeline that could race the matcher. It
// renders the generated PNG and exposes an explicit fallback while the image
// is loading or unavailable.
Item {
    id: root
    property var record: null
    property string side: "left"
    property color accentColor: Theme.accent
    property string title: side === "left" ? L10n.t("timeline.left") : L10n.t("timeline.right")
    property string previewSource: record ? (side === "left" ? String(record.leftPreview || "") : String(record.rightPreview || "")) : ""
    property string sourcePath: record ? (side === "left" ? String(record.leftSource || "") : String(record.rightSource || "")) : ""
    property real timestamp: record ? (side === "left" ? Number(record.leftStart || 0) : Number(record.rightStart || 0)) : 0
    signal frameUnavailable(string side)

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
                Item { width: parent.width - timeText.implicitWidth - 52; height: 1 }
                Text { id: timeText; text: root.timecode(root.timestamp); color: Theme.textSecondary; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter }
            }

            Rectangle {
                width: parent.width
                height: Math.max(80, parent.height - 58)
                color: Theme.well
                radius: 6
                border.color: Theme.hairline
                clip: true

                Image {
                    id: frameImage
                    anchors.fill: parent
                    anchors.margins: 6
                    source: root.previewSource
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    visible: status === Image.Ready
                    onStatusChanged: if (status === Image.Error) root.frameUnavailable(root.side)
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    visible: frameImage.status !== Image.Ready
                    Image { anchors.horizontalCenter: parent.horizontalCenter; source: "qrc:/qt/qml/PfUi/qml/assets/film.svg"; sourceSize.width: 24; sourceSize.height: 24; opacity: 0.72 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: frameImage.status === Image.Loading ? L10n.t("timeline.loadingFrame") : L10n.t("timeline.frameUnavailable"); color: Theme.textSecondary; font.pixelSize: 11 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.timecode(root.timestamp); color: root.accentColor; font.family: Theme.displayFont; font.pixelSize: 16 }
                }
            }

            Text { width: parent.width; text: root.fileName(root.sourcePath); color: Theme.textDisabled; font.pixelSize: 9; elide: Text.ElideMiddle }
        }
    }
}
