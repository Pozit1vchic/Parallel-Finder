import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi
import PfUiBridge

ReviewDialog {
    id: root
    objectName: "pairAdjustDialog"
    title: Review.t("adjust") + (record ? " · " + (Number(record.id) + 1) : "")
    property var record: null
    property real leftStart: 0
    property real leftEnd: 0
    property real rightStart: 0
    property real rightEnd: 0
    property string errorText: ""
    property string previewLeft: ""
    property string previewRight: ""
    property real leftPreviewAt: 0
    property real rightPreviewAt: 0
    property real previewedLeft: -1
    property real previewedRight: -1
    readonly property var draft: record ? Object.assign({}, record, {
        leftClipStart: leftStart, leftClipEnd: leftEnd, rightClipStart: rightStart, rightClipEnd: rightEnd,
        leftStart: leftPreviewAt, leftEnd: leftEnd, rightStart: rightPreviewAt, rightEnd: rightEnd,
        leftPreview: previewLeft || record.leftPreview, rightPreview: previewRight || record.rightPreview}) : null
    function fps(side) {
        const summary = record ? Analysis.summaryForSource(record[side + "Source"]) : {}
        return summary.frameRate > 0 ? summary.frameRate : summary.frameCount > 0 && summary.durationSeconds > 0 ? summary.frameCount / summary.durationSeconds : 24
    }
    function duration(side) {
        const summary = record ? Analysis.summaryForSource(record[side + "Source"]) : {}
        return Number(summary.durationSeconds || (record ? record.duration : 1))
    }
    onLeftStartChanged: leftPreviewAt = leftStart
    onRightStartChanged: rightPreviewAt = rightStart
    onLeftPreviewAtChanged: if (visible) previewTimer.restart()
    onRightPreviewAtChanged: if (visible) previewTimer.restart()
    Timer {
        id: previewTimer; interval: 90
        onTriggered: {
            if (!root.visible || !root.record || (root.previewedLeft === root.leftPreviewAt && root.previewedRight === root.rightPreviewAt)) return
            if (Analysis.reviewBusy) { restart(); return }
            Analysis.previewResultRange(root.record.id, root.leftPreviewAt, root.rightPreviewAt)
        }
    }
    Connections {
        target: Analysis
        function onReviewBusyChanged() { if (!Analysis.reviewBusy && root.visible) previewTimer.restart() }
        function onReviewPreviewsReady(id, leftStart, rightStart, left, right) {
            if (!root.visible || !root.record || Number(root.record.id) !== id || Math.abs(leftStart - root.leftPreviewAt) > 0.000001 || Math.abs(rightStart - root.rightPreviewAt) > 0.000001) return
            root.previewedLeft = leftStart; root.previewedRight = rightStart
            root.previewLeft = left; root.previewRight = right
        }
    }
    function loadRange() {
        if (!record) return
        leftStart = Number(record.leftClipStart !== undefined ? record.leftClipStart : record.leftStart)
        leftEnd = Number(record.leftClipEnd !== undefined ? record.leftClipEnd : record.leftEnd)
        rightStart = Number(record.rightClipStart !== undefined ? record.rightClipStart : record.rightStart)
        rightEnd = Number(record.rightClipEnd !== undefined ? record.rightClipEnd : record.rightEnd)
    }
    function step(side, direction) {
        if (!record) return
        const frameRate = fps(side)
        const delta = direction / frameRate
        if (root[side + "Start"] + delta < 0 || root[side + "End"] + delta > duration(side)) return
        root[side + "Start"] += delta; root[side + "End"] += delta
    }
    onAboutToShow: { backdropClosing = false; errorText = ""; previewLeft = ""; previewRight = ""; loadRange(); leftPreviewAt = leftStart; rightPreviewAt = rightStart; previewedLeft = -1; previewedRight = -1; previewTimer.restart() }
    onClosed: { previewTimer.stop(); leftPreview.stopPlayback(); rightPreview.stopPlayback() }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        Text { Layout.fillWidth: true; text: Review.t("rangeHint"); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            ComparisonView { id: leftPreview; objectName: "adjustLeftView"; Layout.fillWidth: true; Layout.fillHeight: true; side: "left"; record: root.draft; onPreviewRequested: togglePlayback() }
            ComparisonView { id: rightPreview; objectName: "adjustRightView"; Layout.fillWidth: true; Layout.fillHeight: true; side: "right"; record: root.draft; accentColor: Theme.sage; onPreviewRequested: togglePlayback() }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Repeater {
                model: ["left", "right"]
                delegate: ColumnLayout {
                    required property string modelData
                    Layout.fillWidth: true; spacing: 6
                    PairTimeline {
                        id: sideTimeline
                        Layout.fillWidth: true
                        property string side: parent.modelData
                        label: Review.t(side)
                        startTime: root[side + "Start"]; endTime: root[side + "End"]
                        duration: root.duration(side); fps: root.fps(side)
                        onRangeEdited: function(start, end) {
                            if (end - start < 1 / fps) return
                            const movedEnd = Math.abs(end - root[side + "End"]) > 0.000001
                            root[side + "Start"] = start; root[side + "End"] = end
                            root[side + "PreviewAt"] = movedEnd ? Math.max(start, end - 1 / fps) : start
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        PfButton { compact: true; quiet: true; Layout.fillWidth: true; text: "−1 " + (L10n.language === "ru" ? "кадр" : "frame"); onClicked: root.step(modelData, -1) }
                        PfButton { compact: true; quiet: true; Layout.fillWidth: true; text: "+1 " + (L10n.language === "ru" ? "кадр" : "frame"); onClicked: root.step(modelData, 1) }
                        PfButton { compact: true; quiet: true; text: "▶"; Accessible.name: Review.t("preview"); onClicked: modelData === "left" ? leftPreview.togglePlayback() : rightPreview.togglePlayback() }
                    }
                }
            }
        }
        Text { visible: !!root.errorText; Layout.fillWidth: true; text: root.errorText; color: Theme.accent; wrapMode: Text.WordWrap; font.family: Theme.fontFamily; font.pixelSize: 11 }
        RowLayout {
            Layout.fillWidth: true
            PfButton { quiet: true; text: Review.t("original"); enabled: !!root.record && !!root.record.originalRange && !Analysis.reviewBusy; onClicked: if (Analysis.resetResultRange(root.record.id)) root.close() }
            Item { Layout.fillWidth: true }
            PfButton { primary: true; text: Review.t("apply"); enabled: !!root.record && !Analysis.reviewBusy && !Analysis.exportBusy; onClicked: {
                if (Analysis.setResultRange(root.record.id, root.leftStart, root.leftEnd, root.rightStart, root.rightEnd)) root.close()
                else root.errorText = Review.t("invalid")
            } }
        }
    }
}
