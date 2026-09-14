import QtQuick
import PfUi
import PfUiBridge

Rectangle {
    id: root
    property var analysis: Analysis
    height: 68; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
    function timecode(seconds) {
        const total = Math.max(0, Math.round(Number(seconds) || 0))
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        return [h, m, s].map(function (v) { return String(v).padStart(2, "0") }).join(":")
    }
    Row {
        anchors.fill: parent; anchors.margins: 1
        Repeater {
            model: [
                { label: L10n.t("stats.files"), value: root.analysis.fileCount },
                { label: L10n.t("stats.frames"), value: root.analysis.frameCount },
                { label: L10n.t("stats.scenes"), value: root.analysis.sceneCount },
                { label: L10n.t("stats.pairs"), value: root.analysis.matchCount },
                { label: L10n.t("stats.duration"), value: root.timecode(root.analysis.durationSeconds) },
                { label: L10n.t("stats.progress"), value: root.analysis.busy || root.analysis.analysisCompleted ? Math.round(root.analysis.progress * 100) + "%" : L10n.t("common.empty") }
            ]
            delegate: Item {
                width: parent.width / 6; height: parent.height
                Rectangle { visible: index > 0; x: 0; y: 16; width: 1; height: parent.height - 32; color: Theme.hairline }
                Column { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    Text { text: modelData.label; color: Theme.textDisabled; font.pixelSize: 10 }
                    Text { text: modelData.value; color: index === 3 ? Theme.accent : Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 21 }
                }
            }
        }
    }
}
