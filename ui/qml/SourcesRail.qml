import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import PfUi
import PfUiBridge

Rectangle {
    id: root
    property var sourceFiles: []
    property int selectedSourceIndex: -1
    property bool advancedOpen: false
    property string accuracyPreset: Analysis.accuracyPreset
    property var modelFiles: [
        "yolov8n-pose.onnx", "yolov8s-pose.onnx", "yolov8m-pose.onnx", "yolov8l-pose.onnx", "yolov8x-pose.onnx",
        "yolo11n-pose.onnx", "yolo11s-pose.onnx", "yolo11m-pose.onnx", "yolo11l-pose.onnx", "yolo11x-pose.onnx",
        "yolo26n-pose.onnx", "yolo26s-pose.onnx", "yolo26m-pose.onnx", "yolo26l-pose.onnx", "yolo26x-pose.onnx",
        "yolo26m-pose-640-b1.onnx"
    ]
    property var modelBaseLabels: [
        "YOLOv8 · nano", "YOLOv8 · small", "YOLOv8 · medium", "YOLOv8 · large", "YOLOv8 · xlarge",
        "YOLO11 · nano", "YOLO11 · small", "YOLO11 · medium", "YOLO11 · large", "YOLO11 · xlarge",
        "YOLO26 · nano", "YOLO26 · small", "YOLO26 · medium", "YOLO26 · large", "YOLO26 · xlarge",
        "YOLO26 · medium · 640 · batch 1"
    ]
    property var modelLabels: []
    signal filesRequested(var urls)
    signal folderRequested()
    signal clearRequested()
    signal removeRequested(int index)
    signal analyzeRequested()
    width: Theme.sidePanelWidth; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
    layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowPanel; shadowBlur: 0.75; shadowVerticalOffset: 10 }

    function sourceName(path) {
        const pieces = String(path).replace(/\\/g, "/").split("/")
        return pieces[pieces.length - 1] || path
    }

    function refreshModelLabels() {
        var labels = []
        for (var i = 0; i < modelFiles.length; ++i) {
            var state = Analysis.modelAvailable(modelFiles[i]) ? "✓ " : "↓ "
            labels.push(state + modelBaseLabels[i])
        }
        modelLabels = labels
    }

    Component.onCompleted: refreshModelLabels()

    Connections {
        target: Analysis
        function onModelCatalogChanged() { root.refreshModelLabels() }
        function onModelStatusChanged() { root.refreshModelLabels() }
    }

    function applyAccuracyPreset(preset) {
        Analysis.accuracyPreset = preset
        if (preset === "fast") {
            Analysis.similarityThreshold = 0.72; Analysis.candidateThreshold = 0.40
            Analysis.repeatGap = 8.0; Analysis.sameFileGap = 3.0; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 2.0; Analysis.noiseFactor = 1.25
            Analysis.maxUniqueResults = 50; Analysis.timeWeight = 0.10
        } else if (preset === "precise") {
            Analysis.similarityThreshold = 0.90; Analysis.candidateThreshold = 0.70
            Analysis.repeatGap = 4.0; Analysis.sameFileGap = 1.5; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 1.0; Analysis.noiseFactor = 0.70
            Analysis.maxUniqueResults = 200; Analysis.timeWeight = 0.40
        } else {
            Analysis.similarityThreshold = 0.85; Analysis.candidateThreshold = 0.55
            Analysis.repeatGap = 6.0; Analysis.sameFileGap = 2.0; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 1.5; Analysis.noiseFactor = 1.0
            Analysis.maxUniqueResults = 100; Analysis.timeWeight = 0.25
        }
    }

    function markCustom() { Analysis.accuracyPreset = "custom" }

    Flickable {
        anchors.fill: parent; anchors.margins: 16; anchors.bottomMargin: 86; clip: true; contentWidth: width; contentHeight: content.implicitHeight + 18
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: content; width: parent.width; spacing: 10
            RowLayout { Layout.fillWidth: true; height: 42
                ColumnLayout { Layout.fillWidth: true; spacing: 3
                    Text { id: sourceTitle; text: L10n.t("sources.title"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: root.sourceFiles.length > 0 ? root.sourceFiles.length + " " + L10n.t("sources.loaded") : L10n.t("sources.subtitle"); color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight }
                }
            }
            DropArea {
                Layout.fillWidth: true
                width: parent.width; height: 102
                Accessible.name: L10n.t("sources.dropTitle")
                onDropped: if (drop.hasUrls) root.filesRequested(drop.urls)
                Rectangle { anchors.fill: parent; radius: Theme.radiusButton; color: parent.containsDrag ? Theme.accentMuted : Theme.well; border.width: 1; border.color: parent.containsDrag ? Theme.accent : Theme.hairlineStrong
                    Column { anchors.centerIn: parent; spacing: 7
                        Image { anchors.horizontalCenter: parent.horizontalCenter; source: "qrc:/qt/qml/PfUi/qml/assets/plus.svg"; sourceSize.width: 20; sourceSize.height: 20; smooth: true }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: L10n.t("sources.dropTitle"); color: Theme.textPrimary; font.pixelSize: 12 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: L10n.t("sources.dropHint"); color: Theme.textSecondary; font.pixelSize: 10 }
                    }
                }
            }
            RowLayout { Layout.fillWidth: true; spacing: 7
                PfButton { Layout.fillWidth: true; text: L10n.t("sources.add"); onClicked: root.filesRequested([]) }
                PfButton { Layout.fillWidth: true; text: L10n.t("sources.folder"); quiet: true; onClicked: root.folderRequested() }
            }
            RowLayout { Layout.fillWidth: true; spacing: 7
                PfButton { Layout.fillWidth: true; text: L10n.t("sources.clear"); quiet: true; enabled: root.sourceFiles.length > 0; onClicked: root.clearRequested() }
                PfButton { Layout.fillWidth: true; text: L10n.t("sources.remove"); quiet: true; enabled: root.selectedSourceIndex >= 0; onClicked: root.removeRequested(root.selectedSourceIndex) }
            }
            ListView {
                Layout.fillWidth: true
                width: parent.width; height: root.sourceFiles.length > 0 ? Math.min(128, root.sourceFiles.length * 30) : 34; clip: true; model: root.sourceFiles; focus: true; activeFocusOnTab: true
                Accessible.name: L10n.t("sources.title")
                Keys.onUpPressed: { root.selectedSourceIndex = Math.max(0, root.selectedSourceIndex < 0 ? 0 : root.selectedSourceIndex - 1); positionViewAtIndex(root.selectedSourceIndex, ListView.Contain); event.accepted = true }
                Keys.onDownPressed: { root.selectedSourceIndex = Math.min(root.sourceFiles.length - 1, root.selectedSourceIndex < 0 ? 0 : root.selectedSourceIndex + 1); positionViewAtIndex(root.selectedSourceIndex, ListView.Contain); event.accepted = true }
                Keys.onReturnPressed: if (root.selectedSourceIndex >= 0) root.removeRequested(root.selectedSourceIndex)
                delegate: Rectangle {
                    width: ListView.view.width; height: 30; radius: 5; color: index === root.selectedSourceIndex ? Theme.accentMuted : (index % 2 === 0 ? Theme.surfaceRaised : "transparent"); Accessible.name: root.sourceName(modelData); Accessible.role: Accessible.ListItem
                    Text { anchors.left: parent.left; anchors.leftMargin: 9; anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; text: (index + 1) + "  " + root.sourceName(modelData); color: Theme.textSecondary; font.pixelSize: 10; elide: Text.ElideMiddle }
                    MouseArea { anchors.fill: parent; onClicked: root.selectedSourceIndex = index }
                }
                Text { anchors.centerIn: parent; visible: root.sourceFiles.length === 0; text: L10n.t("sources.empty"); color: Theme.textDisabled; font.pixelSize: 10 }
            }
            Rectangle { Layout.fillWidth: true; width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: accuracyCard.implicitHeight + 24
                Column { id: accuracyCard; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Text { text: L10n.t("search.accuracyGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                    Text { width: parent.width; text: L10n.t("search.accuracyHint"); color: Theme.textSecondary; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    GridLayout { width: parent.width; columns: 2; columnSpacing: 6; rowSpacing: 6
                        PfButton { Layout.fillWidth: true; compact: true; text: L10n.t("search.presetFast"); quiet: root.accuracyPreset !== "fast"; onClicked: root.applyAccuracyPreset("fast") }
                        PfButton { Layout.fillWidth: true; compact: true; text: L10n.t("search.presetBalanced"); quiet: root.accuracyPreset !== "balanced"; onClicked: root.applyAccuracyPreset("balanced") }
                        PfButton { Layout.fillWidth: true; Layout.columnSpan: 2; compact: true; text: L10n.t("search.presetPrecise"); quiet: root.accuracyPreset !== "precise"; onClicked: root.applyAccuracyPreset("precise") }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: qualityCard.implicitHeight + 24
                Column { id: qualityCard; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout { width: parent.width
                        Text { Layout.fillWidth: true; text: L10n.t("search.frameProcessing"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { text: Analysis.qualityProfile === "fast" ? L10n.t("search.fast") : Analysis.qualityProfile === "medium" ? L10n.t("search.medium") : L10n.t("search.maximum"); color: Theme.sage; font.pixelSize: 10 }
                    }
                    GridLayout { width: parent.width; columns: 2; columnSpacing: 6; rowSpacing: 6
                        PfButton { Layout.fillWidth: true; compact: true; text: L10n.t("search.fast"); quiet: Analysis.qualityProfile !== "fast"; onClicked: Analysis.qualityProfile = "fast" }
                        PfButton { Layout.fillWidth: true; compact: true; text: L10n.t("search.medium"); quiet: Analysis.qualityProfile !== "medium"; onClicked: Analysis.qualityProfile = "medium" }
                        PfButton { Layout.fillWidth: true; Layout.columnSpan: 2; compact: true; text: L10n.t("search.maximum"); quiet: Analysis.qualityProfile !== "maximum"; onClicked: Analysis.qualityProfile = "maximum" }
                    }
                    PfCheckBox { text: L10n.t("search.normalize"); tooltipText: L10n.t("search.normalizeHint"); checked: Analysis.normalizeSize; onToggled: Analysis.normalizeSize = checked }
                    PfCheckBox { text: L10n.t("search.mirror"); tooltipText: L10n.t("search.mirrorHint"); checked: Analysis.mirrorPoses; onToggled: Analysis.mirrorPoses = checked }
                }
            }
            CollapsibleSection {
                Layout.fillWidth: true
                width: parent.width
                title: L10n.t("search.advanced")
                expanded: root.advancedOpen
                onToggled: root.advancedOpen = expanded
                Rectangle { width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: modelCard.implicitHeight + 24
                    Column { id: modelCard; anchors.fill: parent; anchors.margins: 12; spacing: 7
                        Text { text: L10n.t("settings.poseModel"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfComboBox {
                            width: parent.width
                            model: root.modelLabels
                            enabled: !Analysis.modelDownloading
                            currentIndex: Math.max(0, root.modelFiles.indexOf(Analysis.modelChoice))
                            Accessible.name: L10n.t("settings.poseModel")
                            onActivated: Analysis.selectModel(root.modelFiles[currentIndex])
                        }
                        Text {
                            width: parent.width
                            text: Analysis.modelDownloading
                                ? (L10n.t("settings.modelDownloading") + "\n" + Analysis.modelDownloadingName
                                   + " · " + Math.round(Analysis.modelDownloadProgress * 100) + "%")
                                : (Analysis.modelStatus.length ? Analysis.modelStatus : L10n.t("settings.modelHint"))
                            color: Analysis.modelDownloading ? Theme.accent : Theme.textSecondary
                            font.pixelSize: 10; wrapMode: Text.WordWrap; maximumLineCount: 4; clip: true
                        }
                        ProgressBar {
                            width: parent.width
                            visible: Analysis.modelDownloading
                            value: Analysis.modelDownloadProgress
                            indeterminate: Analysis.modelDownloading && Analysis.modelDownloadProgress <= 0
                            Accessible.name: L10n.t("settings.modelDownloading")
                        }
                    }
                }
                Rectangle { width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: sceneCard.implicitHeight + 24
                    Column { id: sceneCard; anchors.fill: parent; anchors.margins: 12; spacing: 7
                        Text { text: L10n.t("search.sceneGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfSliderField { width: parent.width; label: L10n.t("search.sceneThreshold"); from: 8; to: 60; stepSize: 1; value: Analysis.sceneThreshold; decimals: 0; integer: true; tooltipText: L10n.t("search.sceneThresholdHint"); Accessible.name: L10n.t("search.sceneThreshold"); onValueEdited: Analysis.setSceneThreshold(nextValue) }
                    }
                }
                Rectangle { width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: similarityCard.implicitHeight + 24
                    Column { id: similarityCard; anchors.fill: parent; anchors.margins: 12; spacing: 8
                        Text { text: L10n.t("search.similarityGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfSliderField { width: parent.width; label: L10n.t("search.similarity"); from: 0.5; to: 0.99; stepSize: 0.01; value: Analysis.similarityThreshold; displayScale: 100; decimals: 0; suffix: "%"; tooltipText: L10n.t("search.similarityHint"); Accessible.name: L10n.t("search.similarity"); onValueEdited: { root.markCustom(); Analysis.similarityThreshold = nextValue } }
                        PfSliderField { width: parent.width; label: L10n.t("search.candidate"); from: 0.2; to: 0.95; stepSize: 0.01; value: Analysis.candidateThreshold; displayScale: 100; decimals: 0; suffix: "%"; tooltipText: L10n.t("search.candidateHint"); Accessible.name: L10n.t("search.candidate"); onValueEdited: { root.markCustom(); Analysis.candidateThreshold = nextValue } }
                    }
                }
                Rectangle { width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: spacingCard.implicitHeight + 24
                    Column { id: spacingCard; anchors.fill: parent; anchors.margins: 12; spacing: 9
                        Text { text: L10n.t("search.spacingGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfSliderField { width: parent.width; label: L10n.t("search.repeatGap"); from: 0; to: 30; stepSize: 0.5; value: Analysis.repeatGap; decimals: 1; suffix: L10n.t("common.seconds"); tooltipText: L10n.t("search.repeatGapHint"); Accessible.name: L10n.t("search.repeatGap"); onValueEdited: { root.markCustom(); Analysis.repeatGap = nextValue } }
                        PfSliderField { width: parent.width; label: L10n.t("search.sameFileGap"); from: 0; to: 15; stepSize: 0.5; value: Analysis.sameFileGap; decimals: 1; suffix: L10n.t("common.seconds"); tooltipText: L10n.t("search.sameFileGapHint"); Accessible.name: L10n.t("search.sameFileGap"); onValueEdited: { root.markCustom(); Analysis.sameFileGap = nextValue } }
                        PfSliderField { width: parent.width; label: L10n.t("search.crossFileGap"); from: 0; to: 15; stepSize: 0.5; value: Analysis.crossFileGap; decimals: 1; suffix: L10n.t("common.seconds"); tooltipText: L10n.t("search.crossFileGapHint"); Accessible.name: L10n.t("search.crossFileGap"); onValueEdited: { root.markCustom(); Analysis.crossFileGap = nextValue } }
                        PfSliderField { width: parent.width; label: L10n.t("search.duplicateWindow"); from: 0.25; to: 8; stepSize: 0.25; value: Analysis.duplicateWindow; decimals: 2; suffix: L10n.t("common.seconds"); tooltipText: L10n.t("search.duplicateWindowHint"); Accessible.name: L10n.t("search.duplicateWindow"); onValueEdited: { root.markCustom(); Analysis.duplicateWindow = nextValue } }
                    }
                }
                Rectangle { width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: rankingCard.implicitHeight + 24
                    Column { id: rankingCard; anchors.fill: parent; anchors.margins: 12; spacing: 9
                        Text { text: L10n.t("search.rankingGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfSliderField { width: parent.width; label: L10n.t("search.noise"); from: 0; to: 2; stepSize: 0.05; value: Analysis.noiseFactor; decimals: 2; tooltipText: L10n.t("search.noiseHint"); Accessible.name: L10n.t("search.noise"); onValueEdited: { root.markCustom(); Analysis.noiseFactor = nextValue } }
                        PfSliderField { width: parent.width; label: L10n.t("search.maxResults"); from: 10; to: 500; stepSize: 10; value: Analysis.maxUniqueResults; decimals: 0; integer: true; tooltipText: L10n.t("search.maxResultsHint"); Accessible.name: L10n.t("search.maxResults"); onValueEdited: { root.markCustom(); Analysis.maxUniqueResults = Math.round(nextValue) } }
                        PfSliderField { width: parent.width; label: L10n.t("search.timeWeight"); from: 0; to: 1; stepSize: 0.05; value: Analysis.timeWeight; displayScale: 100; decimals: 0; suffix: "%"; tooltipText: L10n.t("search.timeWeightHint"); Accessible.name: L10n.t("search.timeWeight"); onValueEdited: { root.markCustom(); Analysis.timeWeight = nextValue } }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 12; height: 70; color: Theme.rail
        Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: Theme.hairline }
        Column {
            anchors.fill: parent; anchors.topMargin: 10; spacing: 5
            PfButton {
                width: parent.width
                text: root.sourceFiles.length === 0 ? L10n.t("sources.add") : (Analysis.busy ? L10n.t("search.running") : (Analysis.modelDownloading ? L10n.t("settings.modelDownloading") : L10n.t("search.start")))
                enabled: root.sourceFiles.length === 0 || (!Analysis.busy && !Analysis.modelDownloading)
                onClicked: root.sourceFiles.length === 0 ? root.filesRequested([]) : root.analyzeRequested()
                Accessible.name: text
            }
            Text {
                width: parent.width; text: Analysis.busy
                    ? L10n.status(Analysis.progressStage) + " · " + Math.round(Analysis.progress * 100) + "%"
                    : L10n.status(Analysis.status)
                color: Analysis.busy ? Theme.accent : Theme.textDisabled; font.pixelSize: 9
                elide: Text.ElideRight
            }
        }
    }
}
