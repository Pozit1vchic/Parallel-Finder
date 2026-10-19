import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import QtQuick.Layouts
import PfUi
import PfUiBridge

Rectangle {
    id: root
    objectName: "sourcesRail"
    property var sourceFiles: []
    property int selectedSourceIndex: -1
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

    onSourceFilesChanged: {
        if (selectedSourceIndex >= sourceFiles.length) selectedSourceIndex = -1
        if (sourceFlick) sourceFlick.contentY = 0
    }

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
            Analysis.similarityThreshold = 0.70; Analysis.candidateThreshold = 0.48
            Analysis.repeatGap = 8.0; Analysis.sameFileGap = 3.0; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 2.0; Analysis.noiseFactor = 1.25
            Analysis.maxUniqueResults = 500; Analysis.timeWeight = 0.10
        } else if (preset === "precise") {
            Analysis.similarityThreshold = 0.84; Analysis.candidateThreshold = 0.68
            Analysis.repeatGap = 4.0; Analysis.sameFileGap = 1.5; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 1.0; Analysis.noiseFactor = 0.70
            Analysis.maxUniqueResults = 500; Analysis.timeWeight = 0.40
        } else {
            Analysis.similarityThreshold = 0.76; Analysis.candidateThreshold = 0.55
            Analysis.repeatGap = 6.0; Analysis.sameFileGap = 2.0; Analysis.crossFileGap = 0.0
            Analysis.duplicateWindow = 1.5; Analysis.noiseFactor = 1.0
            Analysis.maxUniqueResults = 500; Analysis.timeWeight = 0.25
        }
    }

    function applyProcessingPreset(preset) {
        Analysis.qualityProfile = preset
        // Keep the matcher profile aligned with the sampling/quality choice.
        // Fast, medium and maximum map to the corresponding established
        // matcher profiles without exposing low-level thresholds in the UI.
        root.applyAccuracyPreset(preset === "fast" ? "fast" : preset === "medium" ? "balanced" : "precise")
    }

    Flickable {
        id: sourceFlick
        objectName: "sourceOptionsFlick"
        anchors.fill: parent; anchors.margins: 16; anchors.bottomMargin: 100; clip: true; contentWidth: width; contentHeight: Math.max(content.implicitHeight, content.childrenRect.height) + 18
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
        ColumnLayout {
            id: content; width: parent.width; spacing: 10
            RowLayout { Layout.fillWidth: true; height: 42
                ColumnLayout { Layout.fillWidth: true; spacing: 3
                    Text { font.family: Theme.fontFamily; id: sourceTitle; text: L10n.t("sources.title"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { font.family: Theme.fontFamily; Layout.fillWidth: true; text: root.sourceFiles.length > 0 ? root.sourceFiles.length + " " + L10n.t("sources.loaded") : L10n.t("sources.subtitle"); color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight }
                }
            }
            DropArea {
                id: rootDrop
                objectName: "sourceDropArea"
                Layout.fillWidth: true
                width: parent.width; height: root.sourceFiles.length > 0 ? Math.min(160, Math.max(78, root.sourceFiles.length * 32 + 16)) : 102
                Behavior on height { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic } }
                Accessible.name: L10n.t("sources.dropTitle")
                onDropped: if (drop.hasUrls) root.filesRequested(drop.urls)
                Rectangle { anchors.fill: parent; radius: Theme.radiusButton; color: parent.containsDrag ? Theme.accentMuted : Theme.well
                    Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                    Shape { anchors.fill: parent; ShapePath { strokeColor: rootDrop.containsDrag ? Theme.accent : Theme.hairlineStrong; strokeWidth: 1; strokeStyle: ShapePath.DashLine; dashPattern: [4, 4]; fillColor: "transparent"; PathRectangle { x: 0.5; y: 0.5; width: rootDrop.width - 1; height: rootDrop.height - 1; radius: Theme.radiusButton } } }
                    Column { visible: root.sourceFiles.length === 0; anchors.centerIn: parent; width: parent.width - 24; spacing: 7
                        Text { font.family: Theme.fontFamily; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: L10n.t("sources.dropTitle"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap; maximumLineCount: 2; clip: true }
                        Text { font.family: Theme.fontFamily; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: L10n.t("sources.dropHint"); color: Theme.textSecondary; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap; maximumLineCount: 2; clip: true }
                    }
                }
            ListView {
                id: loadedSourcesList; objectName: "loadedSourcesList"; anchors.fill: parent; anchors.margins: 8; visible: root.sourceFiles.length > 0; boundsBehavior: Flickable.StopAtBounds; clip: true; model: root.sourceFiles; focus: true; activeFocusOnTab: true
                // Own the wheel even at the ends; the settings rail must not jump.
                WheelHandler {
                    objectName: "sourceListWheelHandler"
                    target: null
                    onWheel: function(event) {
                        loadedSourcesList.contentY = Math.max(0,Math.min(
                            Math.max(0,loadedSourcesList.contentHeight-loadedSourcesList.height),
                            loadedSourcesList.contentY - (event.pixelDelta.y || event.angleDelta.y / 3)))
                        event.accepted = true
                    }
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
                Accessible.name: L10n.t("sources.title")
                Keys.onUpPressed: { root.selectedSourceIndex = Math.max(0, root.selectedSourceIndex < 0 ? 0 : root.selectedSourceIndex - 1); positionViewAtIndex(root.selectedSourceIndex, ListView.Contain); event.accepted = true }
                Keys.onDownPressed: { root.selectedSourceIndex = Math.min(root.sourceFiles.length - 1, root.selectedSourceIndex < 0 ? 0 : root.selectedSourceIndex + 1); positionViewAtIndex(root.selectedSourceIndex, ListView.Contain); event.accepted = true }
                Keys.onReturnPressed: if (root.selectedSourceIndex >= 0) root.removeRequested(root.selectedSourceIndex)
                delegate: Rectangle {
                    id: sourceCard
                    Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                    property var identitySummary: { Analysis.sourceSummaries; return Analysis.summaryForSource(String(modelData)) }
                    readonly property bool identityExcluded: Analysis.analysisCompleted && identitySummary.identityAdmitted === false
                    width: ListView.view.width; height: 30; radius: 5; color: index === root.selectedSourceIndex ? Theme.accentMuted : (index % 2 === 0 ? Theme.surfaceRaised : "transparent"); Accessible.name: root.sourceName(modelData); Accessible.role: Accessible.ListItem
                    Text { font.family: Theme.fontFamily; anchors.left: parent.left; anchors.leftMargin: 9; anchors.right: parent.right; anchors.rightMargin: parent.identityExcluded ? 21 : 7; anchors.verticalCenter: parent.verticalCenter; text: (index + 1) + "  " + root.sourceName(modelData); color: Theme.textSecondary; font.pixelSize: 10; elide: Text.ElideMiddle }
                    Rectangle { anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; width: 6; height: 6; radius: 3; color: Theme.accent; visible: parent.identityExcluded }
                    MouseArea {
                        id: sourceHover
                        anchors.fill: parent; hoverEnabled: true; onClicked: root.selectedSourceIndex = index
                    }
                    ToolTip {
                        id: sourceTip
                        visible: sourceHover.containsMouse; delay: 650
                        text: root.sourceName(modelData) + (sourceCard.identityExcluded ? "\n" + L10n.t("sources.identityExcluded") : "")
                        contentItem: Text { text: sourceTip.text; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap; width: Math.min(360,implicitWidth) }
                        background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusButton; border.color: Theme.hairlineStrong }
                    }
                }
                Text { font.family: Theme.fontFamily; anchors.centerIn: parent; visible: root.sourceFiles.length === 0; text: L10n.t("sources.empty"); color: Theme.textDisabled; font.pixelSize: 10 }
            }
            }
            RowLayout { Layout.fillWidth: true; spacing: 7
                PfButton { objectName: "addSourcesButton"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("sources.add"); onClicked: root.filesRequested([]) }
                PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("sources.folder"); quiet: true; onClicked: root.folderRequested() }
            }
            RowLayout { Layout.fillWidth: true; spacing: 7
                PfButton { objectName: "clearSourcesButton"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("sources.clear"); quiet: true; enabled: root.sourceFiles.length > 0; onClicked: root.clearRequested() }
                PfButton { objectName: "removeSourceButton"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("sources.remove"); quiet: true; enabled: root.selectedSourceIndex >= 0 && root.selectedSourceIndex < root.sourceFiles.length; onClicked: root.removeRequested(root.selectedSourceIndex) }
            }
            Rectangle { objectName: "frameProcessingCard"; Layout.fillWidth: true; width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: qualityCard.implicitHeight + 24
                Column { id: qualityCard; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout { width: parent.width
                        Text { font.family: Theme.fontFamily; Layout.fillWidth: true; text: L10n.t("search.frameProcessing"); color: Theme.secondaryText; font.pixelSize: 11; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { font.family: Theme.fontFamily; text: Analysis.qualityProfile === "fast" ? L10n.t("search.fast") : Analysis.qualityProfile === "medium" ? L10n.t("search.medium") : L10n.t("search.maximum"); color: Theme.secondaryText; font.pixelSize: 10 }
                    }
                    Text { font.family: Theme.fontFamily; width: parent.width; text: L10n.t("search.accuracyHint"); color: Theme.textDisabled; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    GridLayout { width: parent.width; columns: 3; columnSpacing: 6; rowSpacing: 6
                        PfButton { objectName: "processingFastButton"; Layout.fillWidth: true; Layout.minimumWidth: 0; compact: true; text: L10n.t("search.fast"); selected: Analysis.qualityProfile === "fast"; onClicked: root.applyProcessingPreset("fast") }
                        PfButton { objectName: "processingMediumButton"; Layout.fillWidth: true; Layout.minimumWidth: 0; compact: true; text: L10n.t("search.medium"); selected: Analysis.qualityProfile === "medium"; onClicked: root.applyProcessingPreset("medium") }
                        PfButton { objectName: "processingMaximumButton"; Layout.fillWidth: true; Layout.minimumWidth: 0; compact: true; text: L10n.t("search.maximum"); selected: Analysis.qualityProfile === "maximum"; onClicked: root.applyProcessingPreset("maximum") }
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
                    Text { font.family: Theme.fontFamily;
                        Layout.fillWidth: true
                        text: L10n.t("search.analysisMode")
                        color: Theme.secondaryText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text { font.family: Theme.fontFamily;
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
                            selected: Analysis.analysisMode === "motion"
                            onClicked: Analysis.analysisMode = "motion"
                        }
                        PfButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            compact: true
                            text: L10n.t("search.modeStatic")
                            selected: Analysis.analysisMode === "static"
                            onClicked: Analysis.analysisMode = "static"
                        }
                        PfButton {
                            Layout.fillWidth: true
                            Layout.columnSpan: 2
                            Layout.minimumWidth: 0
                            compact: true
                            text: L10n.t("search.modeCombined")
                            selected: Analysis.analysisMode === "combined"
                            onClicked: Analysis.analysisMode = "combined"
                        }
                    }
                }
            }
            PfCheckBox {
                objectName: "expandedSearchCheck"
                Layout.fillWidth: true
                text: L10n.t("search.reanalyze")
                tooltipText: L10n.t("search.reanalyzeHint")
                checked: Analysis.expandedSearch
                enabled: !Analysis.busy
                onToggled: Analysis.expandedSearch = checked
            }
            Rectangle { Layout.fillWidth: true; width: parent.width; color: Theme.panelAlt; radius: 10; border.color: Theme.border; implicitHeight: modelCard.implicitHeight + 24
                Column { id: modelCard; anchors.fill: parent; anchors.margins: 12; spacing: 7
                    Text { font.family: Theme.fontFamily; text: L10n.t("settings.poseModel"); color: Theme.secondaryText; font.pixelSize: 11; font.weight: Font.DemiBold }
                    PfComboBox {
                        id: modelCombo
                        objectName: "poseModelComboBox"
                        width: parent.width
                        model: root.modelLabels
                        enabled: !Analysis.modelDownloading
                        function syncModelChoice() {
                            const selected = root.modelFiles.indexOf(Analysis.modelChoice)
                            currentIndex = selected >= 0 ? selected : root.modelFiles.indexOf("yolo26m-pose.onnx")
                        }
                        Component.onCompleted: Qt.callLater(syncModelChoice)
                        onModelChanged: Qt.callLater(syncModelChoice)
                        Connections {
                            target: Analysis
                            function onSettingsChanged() { Qt.callLater(modelCombo.syncModelChoice) }
                        }
                        Accessible.name: L10n.t("settings.poseModel")
                        onActivated: Analysis.selectModel(root.modelFiles[currentIndex])
                    }
                    Text { font.family: Theme.fontFamily;
                        width: parent.width
                        text: Analysis.modelDownloading
                            ? (L10n.t("settings.modelDownloading") + "\n" + Analysis.modelDownloadingName
                               + " · " + Math.round(Analysis.modelDownloadProgress * 100) + "%")
                            : (Analysis.modelStatus.length ? L10n.t(Analysis.modelStatus) : L10n.t("settings.modelHint"))
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
                    objectName: "sourcePrimaryAction"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 42
                    primary: true
                    text: root.sourceFiles.length === 0
                        ? L10n.t("sources.add")
                        : (Analysis.modelDownloading
                            ? L10n.t("settings.modelDownloading")
                            : L10n.t("search.start"))
                    enabled: root.sourceFiles.length === 0 || (!Analysis.busy && !Analysis.modelDownloading && !AppInfo.backendInitializing)
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
            Text { font.family: Theme.fontFamily;
                visible: Analysis.busy
                width: parent.width; text: Analysis.busy
                    ? L10n.status(Analysis.progressStage) + " · " + Math.round(Analysis.progress * 100) + "%"
                    : ""
                color: Analysis.busy ? Theme.accent : Theme.textDisabled; font.pixelSize: 9
                elide: Text.ElideRight
            }
        }
    }
}
