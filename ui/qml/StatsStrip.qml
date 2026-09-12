import QtQuick
import PfUi
import PfUiBridge

Rectangle {
    id: root
    property var analysis: Analysis
    height: 68; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
    Row {
        anchors.fill: parent; anchors.margins: 1
        Repeater {
            model: [
                { label: L10n.t("stats.files"), value: root.analysis.fileCount },
                { label: L10n.t("stats.frames"), value: root.analysis.frameCount },
                { label: L10n.t("stats.scenes"), value: root.analysis.sceneCount },
                { label: L10n.t("stats.pairs"), value: root.analysis.matchCount },
                { label: L10n.t("stats.duration"), value: root.analysis.durationSeconds > 0 ? Math.round(root.analysis.durationSeconds) + " " + L10n.t("common.seconds") : "0 " + L10n.t("common.seconds") },
                { label: L10n.t("stats.status"), value: root.analysis.busy ? Math.round(root.analysis.progress * 100) + "%" : L10n.t("common.empty") }
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
