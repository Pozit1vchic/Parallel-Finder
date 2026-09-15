import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import PfUi
import PfUiBridge

// The center is intentionally a viewport, not a dashboard. Once a pair is
// selected the A/B surfaces take all available height; the old timeline strip
// was removed because it duplicated result timestamps and stole preview area.
ColumnLayout {
    id: root
    focus: true
    activeFocusOnTab: true
    property var sourceFiles: []
    property var selectedRecord: null
    property bool analysisCompleted: Analysis.analysisCompleted && !Analysis.busy
    signal addRequested()
    signal analyzeRequested()
    spacing: 12

    StatsStrip { Layout.fillWidth: true; Layout.preferredHeight: 68; analysis: Analysis }

    Rectangle {
        id: comparisonPanel
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 220
        color: Theme.panel
        radius: Theme.radiusCard
        border.color: Theme.border
        clip: true
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadowPanel
            shadowBlur: 0.72
            shadowVerticalOffset: 12
        }

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Item {
                width: parent.width
                height: 24
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: L10n.t("center.comparison")
                    color: Theme.textPrimary
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    id: stateText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(parent.width * 0.58, implicitWidth)
                    text: Analysis.busy
                        ? L10n.status(Analysis.progressStage)
                        : root.selectedRecord
                            ? Math.round(Number(root.selectedRecord.similarity || 0) * 100) + "% " + L10n.t("center.similarity")
                            : root.analysisCompleted
                                ? (Analysis.matchCount > 0 ? L10n.t("center.completedTitle") : L10n.t("center.noMatchesStatus"))
                                : root.sourceFiles.length > 0 ? L10n.t("center.readyStatus") : L10n.t("center.waiting")
                    color: Analysis.busy ? Theme.accent : Theme.textSecondary
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }

            Item {
                width: parent.width
                height: (Analysis.busy || root.analysisCompleted) ? 20 : 0
                visible: Analysis.busy || root.analysisCompleted
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: L10n.t("stats.progress")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    id: progressText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round(Analysis.progress * 100) + "%"
                    color: Analysis.busy ? Theme.accent : Theme.textSecondary
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                }
            }

            Rectangle {
                width: parent.width
                height: (Analysis.busy || root.analysisCompleted) ? 8 : 0
                visible: Analysis.busy || root.analysisCompleted
                radius: 4
                color: Theme.surfaceMuted
                border.width: 1
                border.color: Theme.hairline
                Accessible.role: Accessible.ProgressBar
                Accessible.name: L10n.t("stats.progress") + ": " + Math.round(Analysis.progress * 100) + "%"
                Rectangle {
                    width: Math.max(0, parent.width * Math.max(0, Math.min(1, Analysis.progress)))
                    height: parent.height - 2
                    y: 1
                    radius: 3
                    color: Analysis.busy ? Theme.accent : Theme.sage
                }
            }

            Rectangle {
                id: stage
                width: parent.width
                height: Math.max(220, parent.height - ((Analysis.busy || root.analysisCompleted) ? 78 : 50))
                color: Theme.well
                radius: Theme.radiusButton
                border.color: Theme.hairline
                clip: true

                Item {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: !!root.selectedRecord
                    Row {
                        anchors.fill: parent
                        spacing: 12
                        ComparisonView {
                            width: (parent.width - 12) / 2
                            height: parent.height
                            record: root.selectedRecord
                            side: "left"
                            accentColor: Theme.accent
                        }
                        ComparisonView {
                            width: (parent.width - 12) / 2
                            height: parent.height
                            record: root.selectedRecord
                            side: "right"
                            accentColor: Theme.sage
                        }
                    }
                }

                Item {
                    anchors.fill: parent
                    anchors.margins: 16
                    visible: !root.selectedRecord
                    Column {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 32, 560)
                        spacing: 9
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Analysis.busy
                                ? L10n.t("center.analyzingTitle")
                                : root.sourceFiles.length === 0
                                    ? L10n.t("center.emptyTitle")
                                    : root.analysisCompleted
                                        ? (Analysis.matchCount > 0 ? L10n.t("center.completedTitle") : L10n.t("center.noMatchesTitle"))
                                        : L10n.t("center.readyTitle")
                            color: Theme.textPrimary
                            font.family: Theme.displayFont
                            font.pixelSize: 20
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Analysis.busy
                                ? L10n.t("center.analyzingHint")
                                : root.sourceFiles.length === 0
                                    ? L10n.t("center.emptyHint")
                                    : root.analysisCompleted
                                        ? (Analysis.matchCount > 0 ? L10n.t("center.completedHint") : L10n.t("center.noMatchesHint"))
                                        : L10n.t("center.readyHint")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }
                        PfButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.sourceFiles.length === 0 && !Analysis.busy
                            text: L10n.t("sources.add")
                            onClicked: root.addRequested()
                        }
                        PfButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.sourceFiles.length > 0 && !Analysis.busy
                            text: root.analysisCompleted ? L10n.t("search.restart") : L10n.t("search.start")
                            onClicked: root.analyzeRequested()
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: Analysis.busy
                            text: L10n.status(Analysis.progressStage) + " · " + Math.round(Analysis.progress * 100) + "%"
                            color: Theme.accent
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
