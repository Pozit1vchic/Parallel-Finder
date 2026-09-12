import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi
import PfUiBridge

Rectangle {
    id: root
    property var sourceFiles: []
    property int selectedSourceIndex: -1
    signal filesRequested(var urls)
    signal folderRequested()
    signal clearRequested()
    signal removeRequested(int index)
    signal analyzeRequested()
    signal resetRequested()
    width: Theme.sidePanelWidth; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
    layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.48); shadowBlur: 0.75; shadowVerticalOffset: 10 }

    function sourceName(path) {
        const pieces = String(path).replace(/\\/g, "/").split("/")
        return pieces[pieces.length - 1] || path
    }

    Flickable {
        anchors.fill: parent; anchors.margins: 16; clip: true; contentWidth: width; contentHeight: content.implicitHeight + 18
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: content; width: parent.width; spacing: 10
            Row { width: parent.width
                Column { width: parent.width - 34; spacing: 4
                    Text { text: L10n.t("sources.title"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { text: root.sourceFiles.length > 0 ? root.sourceFiles.length + " " + L10n.t("sources.loaded") : L10n.t("sources.subtitle"); color: Theme.textSecondary; font.pixelSize: 11 }
                }
                Text { text: root.sourceFiles.length; color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 19 }
            }
            DropArea {
                width: parent.width; height: 102
                onDropped: if (drop.hasUrls) root.filesRequested(drop.urls)
                Rectangle { anchors.fill: parent; radius: Theme.radiusButton; color: parent.containsDrag ? Theme.accentMuted : Theme.well; border.width: 1; border.color: parent.containsDrag ? Theme.accent : Theme.hairlineStrong
                    Column { anchors.centerIn: parent; spacing: 7
                        Image { anchors.horizontalCenter: parent.horizontalCenter; source: "qrc:/qt/qml/PfUi/assets/plus.svg"; sourceSize.width: 20; sourceSize.height: 20; smooth: true }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: L10n.t("sources.dropTitle"); color: Theme.textPrimary; font.pixelSize: 12 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: L10n.t("sources.dropHint"); color: Theme.textSecondary; font.pixelSize: 10 }
                    }
                }
            }
            Row { width: parent.width; spacing: 7
                PfButton { width: (parent.width - 7) / 2; text: L10n.t("sources.add"); onClicked: root.filesRequested([]) }
                PfButton { width: (parent.width - 7) / 2; text: L10n.t("sources.folder"); quiet: true; onClicked: root.folderRequested() }
            }
            Row { width: parent.width; spacing: 7
                PfButton { width: (parent.width - 7) / 2; text: L10n.t("sources.clear"); quiet: true; enabled: root.sourceFiles.length > 0; onClicked: root.clearRequested() }
                PfButton { width: (parent.width - 7) / 2; text: L10n.t("sources.remove"); quiet: true; enabled: root.selectedSourceIndex >= 0; onClicked: root.removeRequested(root.selectedSourceIndex) }
            }
            ListView {
                width: parent.width; height: root.sourceFiles.length > 0 ? Math.min(128, root.sourceFiles.length * 30) : 34; clip: true; model: root.sourceFiles
                delegate: Rectangle {
                    width: ListView.view.width; height: 30; radius: 5; color: index === root.selectedSourceIndex ? Theme.accentMuted : (index % 2 === 0 ? Theme.surfaceRaised : "transparent")
                    Text { anchors.left: parent.left; anchors.leftMargin: 9; anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; text: (index + 1) + "  " + root.sourceName(modelData); color: Theme.textSecondary; font.pixelSize: 10; elide: Text.ElideMiddle }
                    MouseArea { anchors.fill: parent; onClicked: root.selectedSourceIndex = index }
                }
                Text { anchors.centerIn: parent; visible: root.sourceFiles.length === 0; text: L10n.t("sources.empty"); color: Theme.textDisabled; font.pixelSize: 10 }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Text { text: L10n.t("search.similarityGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Column { width: parent.width; spacing: 3
                Row { width: parent.width
                    Text { text: L10n.t("search.similarity"); color: Theme.textSecondary; font.pixelSize: 11 }
                    Item { width: parent.width - 100; height: 1 }
                    Text { text: Math.round(Analysis.similarityThreshold * 100) + "%"; color: Theme.accent; font.pixelSize: 11 }
                }
                PfSlider { width: parent.width; from: 0.5; to: 0.99; stepSize: 0.01; value: Analysis.similarityThreshold; Accessible.name: L10n.t("search.similarity"); onValueChanged: if (Math.abs(Analysis.similarityThreshold - value) > 0.001) Analysis.similarityThreshold = value }
                Row { width: parent.width
                    Text { text: L10n.t("search.candidate"); color: Theme.textSecondary; font.pixelSize: 11 }
                    Item { width: parent.width - 100; height: 1 }
                    Text { text: Math.round(Analysis.candidateThreshold * 100) + "%"; color: Theme.accent; font.pixelSize: 11 }
                }
                PfSlider { width: parent.width; from: 0.2; to: 0.95; stepSize: 0.01; value: Analysis.candidateThreshold; Accessible.name: L10n.t("search.candidate"); onValueChanged: if (Math.abs(Analysis.candidateThreshold - value) > 0.001) Analysis.candidateThreshold = value }
            }
            Text { text: L10n.t("search.spacingGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Column { width: parent.width; spacing: 3
                Text { text: L10n.t("search.repeatGap") + "  ·  " + Analysis.repeatGap.toFixed(1) + " " + L10n.t("common.seconds"); color: Theme.textSecondary; font.pixelSize: 11 }
                PfSlider { width: parent.width; from: 0; to: 30; stepSize: 0.5; value: Analysis.repeatGap; onValueChanged: if (Math.abs(Analysis.repeatGap - value) > 0.001) Analysis.repeatGap = value }
                Text { text: L10n.t("search.sameFileGap") + "  ·  " + Analysis.sameFileGap.toFixed(1) + " " + L10n.t("common.seconds"); color: Theme.textSecondary; font.pixelSize: 11 }
                PfSlider { width: parent.width; from: 0; to: 15; stepSize: 0.5; value: Analysis.sameFileGap; onValueChanged: if (Math.abs(Analysis.sameFileGap - value) > 0.001) Analysis.sameFileGap = value }
                Text { text: L10n.t("search.crossFileGap") + "  ·  " + Analysis.crossFileGap.toFixed(1) + " " + L10n.t("common.seconds"); color: Theme.textSecondary; font.pixelSize: 11 }
                PfSlider { width: parent.width; from: 0; to: 15; stepSize: 0.5; value: Analysis.crossFileGap; onValueChanged: if (Math.abs(Analysis.crossFileGap - value) > 0.001) Analysis.crossFileGap = value }
                Text { text: L10n.t("search.duplicateWindow") + "  ·  " + Analysis.duplicateWindow.toFixed(1) + " " + L10n.t("common.seconds"); color: Theme.textSecondary; font.pixelSize: 11 }
                PfSlider { width: parent.width; from: 0.25; to: 8; stepSize: 0.25; value: Analysis.duplicateWindow; onValueChanged: if (Math.abs(Analysis.duplicateWindow - value) > 0.001) Analysis.duplicateWindow = value }
            }
            Text { text: L10n.t("search.rankingGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Column { width: parent.width; spacing: 3
                Text { text: L10n.t("search.noise") + "  ·  " + Analysis.noiseFactor.toFixed(2); color: Theme.textSecondary; font.pixelSize: 11 }
                PfSlider { width: parent.width; from: 0; to: 2; stepSize: 0.05; value: Analysis.noiseFactor; onValueChanged: if (Math.abs(Analysis.noiseFactor - value) > 0.001) Analysis.noiseFactor = value }
                Text { text: L10n.t("search.maxResults") + "  ·  " + Analysis.maxUniqueResults; color: Theme.textSecondary; font.pixelSize: 11 }
                PfSlider { width: parent.width; from: 10; to: 500; stepSize: 10; value: Analysis.maxUniqueResults; onValueChanged: if (Analysis.maxUniqueResults !== Math.round(value)) Analysis.maxUniqueResults = Math.round(value) }
                Text { text: L10n.t("search.timeWeight") + "  ·  " + Math.round(Analysis.timeWeight * 100) + "%"; color: Theme.textSecondary; font.pixelSize: 11 }
                PfSlider { width: parent.width; from: 0; to: 1; stepSize: 0.05; value: Analysis.timeWeight; onValueChanged: if (Math.abs(Analysis.timeWeight - value) > 0.001) Analysis.timeWeight = value }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Row { width: parent.width
                Text { text: L10n.t("search.quality"); color: Theme.textSecondary; font.pixelSize: 11 }
                Item { width: parent.width - 92; height: 1 }
                Text { text: Analysis.qualityProfile; color: Theme.sage; font.pixelSize: 10 }
            }
            Row { width: parent.width; spacing: 5
                PfButton { width: (parent.width - 10) / 3; text: L10n.t("search.fast"); quiet: Analysis.qualityProfile !== "fast"; onClicked: Analysis.qualityProfile = "fast" }
                PfButton { width: (parent.width - 10) / 3; text: L10n.t("search.medium"); quiet: Analysis.qualityProfile !== "medium"; onClicked: Analysis.qualityProfile = "medium" }
                PfButton { width: (parent.width - 10) / 3; text: L10n.t("search.maximum"); sageAction: Analysis.qualityProfile === "maximum"; quiet: Analysis.qualityProfile !== "maximum"; onClicked: Analysis.qualityProfile = "maximum" }
            }
            PfCheckBox { text: L10n.t("search.normalize"); checked: Analysis.normalizeSize; onToggled: Analysis.normalizeSize = checked }
            PfCheckBox { text: L10n.t("search.mirror"); checked: Analysis.mirrorPoses; onToggled: Analysis.mirrorPoses = checked }
            PfButton { width: parent.width; text: Analysis.busy ? L10n.t("search.running") : L10n.t("search.start"); enabled: root.sourceFiles.length > 0 && !Analysis.busy; onClicked: root.analyzeRequested() }
            Text { width: parent.width; text: Analysis.busy ? Analysis.progressStage + " · " + Math.round(Analysis.progress * 100) + "%" : Analysis.status; color: Theme.textDisabled; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
    }
}
