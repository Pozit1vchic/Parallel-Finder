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
        "yolo26n-pose.onnx", "yolo26s-pose.onnx", "yolo26m-pose.onnx", "yolo26l-pose.onnx", "yolo26x-pose.onnx"
    ]
    property var modelBaseLabels: [
        "YOLOv8 · nano", "YOLOv8 · small", "YOLOv8 · medium", "YOLOv8 · large", "YOLOv8 · xlarge",
        "YOLO11 · nano", "YOLO11 · small", "YOLO11 · medium", "YOLO11 · large", "YOLO11 · xlarge",
        "YOLO26 · nano", "YOLO26 · small", "YOLO26 · medium", "YOLO26 · large", "YOLO26 · xlarge"
    ]
    property var modelLabels: []
    signal filesRequested(var urls)
    signal folderRequested()
    signal clearRequested()
    signal removeRequested(int index)
    signal analyzeRequested()
    implicitWidth: Theme.sidePanelWidth; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
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
            // Fast changes sampling density, not the meaning of a match.
            // Keep a real motion gate so a low-latency scan does not fill the
            // results rail with unrelated people and static close-ups.
            Analysis.similarityThreshold = 0.65; Analysis.candidateThreshold = 0.45
            Analysis.repeatGap = 8.0; Analysis.sameFileGap = 3.0; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 2.0; Analysis.noiseFactor = 1.25
            Analysis.maxUniqueResults = 50; Analysis.timeWeight = 0.10
        } else if (preset === "precise") {
            Analysis.similarityThreshold = 0.82; Analysis.candidateThreshold = 0.65
            Analysis.repeatGap = 4.0; Analysis.sameFileGap = 1.5; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 1.0; Analysis.noiseFactor = 0.70
            Analysis.maxUniqueResults = 200; Analysis.timeWeight = 0.40
        } else {
            Analysis.similarityThreshold = 0.72; Analysis.candidateThreshold = 0.50
            Analysis.repeatGap = 6.0; Analysis.sameFileGap = 2.0; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 1.5; Analysis.noiseFactor = 1.0
            Analysis.maxUniqueResults = 100; Analysis.timeWeight = 0.25
        }
    }

    function markCustom() { Analysis.accuracyPreset = "custom" }

    Flickable {
        anchors.fill: parent; anchors.margins: 16; anchors.bottomMargin: 100; clip: true; contentWidth: width; contentHeight: Math.max(content.implicitHeight, content.childrenRect.height) + 18
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
                    Column { anchors.centerIn: parent; width: parent.width - 24; spacing: 7
                        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: L10n.t("sources.dropTitle"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap; maximumLineCount: 2; clip: true }
                        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: L10n.t("sources.dropHint"); color: Theme.textSecondary; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap; maximumLineCount: 2; clip: true }
                    }
                }
            }
            RowLayout { Layout.fillWidth: true; spacing: 7
                PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("sources.add"); onClicked: root.filesRequested([]) }
                PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("sources.folder"); quiet: true; onClicked: root.folderRequested() }
            }
            RowLayout { Layout.fillWidth: true; spacing: 7
                PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("sources.clear"); quiet: true; enabled: root.sourceFiles.length > 0; onClicked: root.clearRequested() }
                PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("sources.remove"); quiet: true; enabled: root.selectedSourceIndex >= 0; onClicked: root.removeRequested(root.selectedSourceIndex) }
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
                        PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; compact: true; text: L10n.t("search.presetFast"); quiet: root.accuracyPreset !== "fast"; onClicked: root.applyAccuracyPreset("fast") }
                        PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; compact: true; text: L10n.t("search.presetBalanced"); quiet: root.accuracyPreset !== "balanced"; onClicked: root.applyAccuracyPreset("balanced") }
                        PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.columnSpan: 2; compact: true; text: L10n.t("search.presetPrecise"); quiet: root.accuracyPreset !== "precise"; onClicked: root.applyAccuracyPreset("precise") }
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
                        PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; compact: true; text: L10n.t("search.fast"); quiet: Analysis.qualityProfile !== "fast"; onClicked: Analysis.qualityProfile = "fast" }
                        PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; compact: true; text: L10n.t("search.medium"); quiet: Analysis.qualityProfile !== "medium"; onClicked: Analysis.qualityProfile = "medium" }
                        PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.columnSpan: 2; compact: true; text: L10n.t("search.maximum"); quiet: Analysis.qualityProfile !== "maximum"; onClicked: Analysis.qualityProfile = "maximum" }
                    }
                    PfCheckBox { text: L10n.t("search.normalize"); tooltipText: L10n.t("search.normalizeHint"); checked: Analysis.normalizeSize; onToggled: Analysis.normalizeSize = checked }
                    PfCheckBox { text: L10n.t("search.mirror"); tooltipText: L10n.t("search.mirrorHint"); checked: Analysis.mirrorPoses; onToggled: Analysis.mirrorPoses = checked }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                width: parent.width
                color: Theme.panelAlt
                radius: 10
                border.color: Theme.border
                implicitHeight: modeCard.implicitHeight + 24
                ColumnLayout {
                    id: modeCard
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: L10n.t("search.analysisMode")
                        color: Theme.sage
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: L10n.t("search.modeHint")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 6
                        rowSpacing: 6
                        PfButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            compact: true
                            text: L10n.t("search.modeMotion")
                            quiet: Analysis.analysisMode !== "motion"
                            onClicked: Analysis.analysisMode = "motion"
                        }
                        PfButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            compact: true
                            text: L10n.t("search.modeStatic")
                            quiet: Analysis.analysisMode !== "static"
                            onClicked: Analysis.analysisMode = "static"
                        }
                        PfButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            compact: true
                            text: L10n.t("search.modeClips")
                            quiet: Analysis.analysisMode !== "clips"
                            onClicked: Analysis.analysisMode = "clips"
                        }
                        PfButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            compact: true
                            text: L10n.t("search.modeCombined")
                            quiet: Analysis.analysisMode !== "combined"
                            onClicked: Analysis.analysisMode = "combined"
                        }
                    }
                }
            }
            CollapsibleSection {
                Layout.fillWidth: true
                width: parent.width
                title: L10n.t("search.advanced")
                expanded: root.advancedOpen
                onToggled: root.advancedOpen = expanded
                Text {
                    width: parent.width
                    text: L10n.t("search.advancedHint")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
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
                    }
                }
                Rectangle { width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: spacingCard.implicitHeight + 24
                    Column { id: spacingCard; anchors.fill: parent; anchors.margins: 12; spacing: 9
                        Text { text: L10n.t("search.spacingGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfSliderField { width: parent.width; label: L10n.t("search.repeatGap"); from: 0; to: 30; stepSize: 0.5; value: Analysis.repeatGap; decimals: 1; suffix: L10n.t("common.seconds"); tooltipText: L10n.t("search.repeatGapHint"); Accessible.name: L10n.t("search.repeatGap"); onValueEdited: { root.markCustom(); Analysis.repeatGap = nextValue } }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 12; height: 84; color: Theme.rail
        Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: Theme.hairline }
        Column {
            anchors.fill: parent; anchors.topMargin: 10; spacing: 5
            RowLayout {
                width: parent.width
                spacing: 7
                PfButton {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 42
                    text: root.sourceFiles.length === 0
                        ? L10n.t("sources.add")
                        : (Analysis.modelDownloading
                            ? L10n.t("settings.modelDownloading")
                            : L10n.t("search.start"))
                    enabled: root.sourceFiles.length === 0 || (!Analysis.busy && !Analysis.modelDownloading)
                    onClicked: root.sourceFiles.length === 0 ? root.filesRequested([]) : root.analyzeRequested()
                    Accessible.name: text
                }
                PfButton {
                    Layout.preferredWidth: 82
                    Layout.minimumWidth: 76
                    Layout.preferredHeight: 42
                    visible: Analysis.busy
                    text: L10n.t("search.stop")
                    quiet: true
                    onClicked: Analysis.stopAnalysis()
                    Accessible.name: text
                }
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
