import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi
import PfUiBridge

Rectangle {
    id: root
    objectName: "statsStrip"
    property var analysis: Analysis
    property string selectedSource: ""
    readonly property var summary: {
        // Read the notifying property before resolving normalized file paths.
        const summaries = analysis.sourceSummaries
        return selectedSource.length > 0 ? analysis.summaryForSource(selectedSource) : analysis
    }
    implicitHeight: 68; height: 68; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
    function timecode(seconds) {
        const total = Math.max(0, Math.round(Number(seconds) || 0))
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        return [h, m, s].map(function (v) { return String(v).padStart(2, "0") }).join(":")
    }
    RowLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0
        Repeater {
            model: [
                { label: L10n.t("stats.files"), value: root.summary.fileCount || 0 },
                { label: L10n.t("stats.frames"), value: root.summary.frameCount || 0 },
                { label: L10n.t("stats.scenes"), value: root.summary.sceneCount || 0 },
                { label: L10n.t("stats.pairs"), value: root.summary.matchCount || 0 },
                { label: L10n.t("stats.duration"), value: root.timecode(root.summary.durationSeconds) },
                { label: L10n.t("stats.progress"), value: root.analysis.busy || root.analysis.analysisCompleted ? Math.round(root.analysis.progress * 100) + "%" : L10n.t("common.empty") }
            ]
            delegate: Item {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 1; height: parent.height
                ToolTip.visible: statsHover.containsMouse && index === 1
                ToolTip.text: (L10n.language === "ru" ? "Кадры исходника: " : "Source frames: ") + (root.summary.frameCount || 0) + "\n"
                    + (L10n.language === "ru" ? "Декодировано за этот запуск: " : "Decoded this run: ") + root.analysis.decodedFrames + "\n"
                    + (L10n.language === "ru" ? "Выбрано для распознавания за этот запуск: " : "Selected for recognition this run: ") + root.analysis.analyzedFrames
                    + (L10n.language === "ru" ? "\nСчётчики запуска общие для всех видео; кеш не включён." : "\nRun counts cover all videos; cached analysis is excluded.")
                MouseArea { id: statsHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                Rectangle { visible: index > 0; x: 0; y: 16; width: 1; height: parent.height - 32; color: Theme.hairline }
                Column {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 14; anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    Text { font.family: Theme.fontFamily; width: parent.width; text: modelData.label; color: Theme.textDisabled; font.pixelSize: 10; elide: Text.ElideRight; clip: true }
                    Text { width: parent.width; text: modelData.value; color: index === 3 ? Theme.accent : Theme.textPrimary; font.family: Theme.monoFont; font.pixelSize: 18; elide: Text.ElideRight; clip: true }
                }
            }
        }
    }
}
