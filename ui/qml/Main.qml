import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import PfUi
import PfUiBridge

ApplicationWindow {
    id: root
    width: 1360
    height: 860
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: L10n.t("app.title")
    color: Theme.canvas

    property var sourceFiles: []
    property int selectedResultIndex: -1
    property var selectedExportRows: ({})
    property bool hasSelectedExport: Object.keys(selectedExportRows).length > 0
    property string activeMovementFilter: "all"
    property real railWidth: Math.max(272, Math.min(300, width * 0.22))

    function sourceName(path) {
        const pieces = String(path).replace(/\\/g, "/").split("/")
        return pieces[pieces.length - 1] || path
    }

    function addFiles(urls) {
        for (const url of urls) {
            const path = decodeURIComponent(url.toString().replace(/^file:\/\//, ""))
            if (root.sourceFiles.indexOf(path) < 0) root.sourceFiles.push(path)
        }
        root.sourceFilesChanged()
        Analysis.inspectFiles(root.sourceFiles)
    }

    function clearResultsSelection() {
        root.selectedExportRows = ({})
        root.selectedResultIndex = -1
    }

    function resetMatcherSettings() {
        Analysis.similarityThreshold = 0.85
        Analysis.candidateThreshold = 0.55
        Analysis.repeatGap = 6.0
        Analysis.sameFileGap = 2.0
        Analysis.crossFileGap = 0.0
        Analysis.duplicateWindow = 1.5
        Analysis.noiseFactor = 1.0
        Analysis.maxUniqueResults = 100
        Analysis.timeWeight = 0.25
    }

    FileDialog {
        id: fileDialog
        title: L10n.t("dialog.chooseVideos")
        fileMode: FileDialog.OpenFiles
        nameFilters: [L10n.t("dialog.videoFilter"), L10n.t("dialog.allFiles")]
        onAccepted: root.addFiles(selectedFiles)
    }

    FolderDialog { id: folderDialog; title: L10n.t("dialog.chooseFolder") }

    Connections {
        target: Analysis
        function onSummaryChanged() { root.clearResultsSelection() }
    }

    Popup {
        id: settingsDialog
        modal: true; focus: true
        width: Math.min(700, root.width - 40); height: Math.min(650, root.height - 40); padding: 0
        x: Math.round((root.width - width) / 2); y: Math.round((root.height - height) / 2)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.66) }
        enter: Transition { NumberAnimation { properties: "opacity,scale"; from: 0.92; to: 1; duration: 160; easing.type: Easing.OutCubic } }
        exit: Transition { NumberAnimation { properties: "opacity,scale"; to: 0.92; duration: 120 } }
        background: Rectangle {
            color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairline
            layer.enabled: true
            layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.78); shadowBlur: 1.0; shadowVerticalOffset: 18 }
        }
        contentItem: Column {
            spacing: 0
            Rectangle {
                width: parent.width; height: 58; color: Theme.surfaceRaised; radius: Theme.radiusOverlay
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
                Text { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; text: L10n.t("settings.windowTitle"); color: Theme.textPrimary; font.pixelSize: 13; font.weight: Font.DemiBold }
                PfIconButton { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; iconSource: "qrc:/qt/qml/PfUi/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: settingsDialog.close() }
                MouseArea {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.rightMargin: 58
                    property real pressX; property real pressY
                    onPressed: { pressX = mouse.x; pressY = mouse.y }
                    onPositionChanged: if (pressed) {
                        settingsDialog.x = Math.max(12, Math.min(root.width - settingsDialog.width - 12, settingsDialog.x + mouse.x - pressX))
                        settingsDialog.y = Math.max(12, Math.min(root.height - settingsDialog.height - 12, settingsDialog.y + mouse.y - pressY))
                    }
                }
            }
            Flickable {
                width: parent.width; height: parent.height - 58; clip: true; contentWidth: width; contentHeight: settingsContent.implicitHeight + 44
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                Column {
                    id: settingsContent; width: parent.width - 48; x: 24; y: 24; spacing: 14
                    Text { text: L10n.t("settings.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                    Text { text: L10n.t("settings.subtitle"); color: Theme.textSecondary; font.pixelSize: 12 }
                    Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                    Text { text: L10n.t("settings.environment"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                    Row { width: parent.width; height: 34; spacing: 16
                        Text { width: 150; text: L10n.t("settings.provider"); color: Theme.textPrimary; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                        ComboBox { width: 210; height: 32; model: ["Auto", "TensorRT", "CUDA", "DirectML", "CPU"]; currentIndex: 0 }
                        Text { text: AppInfo.gpuSummary; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width - 390 }
                    }
                    Row { width: parent.width; height: 28; spacing: 16; Text { width: 150; text: L10n.t("settings.theme"); color: Theme.textPrimary; font.pixelSize: 12 }
Text { text: L10n.t("settings.themeValue"); color: Theme.textSecondary; font.pixelSize: 12 } }
                    Row { width: parent.width; height: 28; spacing: 16; Text { width: 150; text: L10n.t("settings.language"); color: Theme.textPrimary; font.pixelSize: 12 }
Text { text: L10n.t("settings.languageValue"); color: Theme.textSecondary; font.pixelSize: 12 } }
                    Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                    Text { text: L10n.t("settings.models"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                    Row { width: parent.width; height: 30; spacing: 16; Text { width: 150; text: L10n.t("settings.poseModel"); color: Theme.textPrimary; font.pixelSize: 12 }
Text { text: "models / yolo26m-pose.onnx"; color: Theme.textSecondary; elide: Text.ElideMiddle; width: parent.width - 170; font.pixelSize: 11 } }
                    Row { width: parent.width; height: 30; spacing: 16; Text { width: 150; text: L10n.t("settings.cache"); color: Theme.textPrimary; font.pixelSize: 12 }
Text { text: "8 ГБ · %LocalAppData%/ParallelFinder"; color: Theme.textSecondary; elide: Text.ElideMiddle; width: parent.width - 170; font.pixelSize: 11 } }
                    Row { width: parent.width; height: 30; spacing: 16; Text { width: 150; text: L10n.t("settings.scene"); color: Theme.textPrimary; font.pixelSize: 12 }
Text { text: "HSV · 27 · 8 кадров"; color: Theme.textSecondary; font.pixelSize: 11 } }
                    Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                    Text { text: L10n.t("settings.matcher"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                    Text { text: L10n.t("settings.matcherHint"); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                    PfButton { width: parent.width; text: L10n.t("settings.reset"); quiet: true; onClicked: root.resetMatcherSettings() }
                }
            }
        }
    }

    Popup {
        id: exportDialog
        modal: true; focus: true
        width: Math.min(560, root.width - 40); height: 430; padding: 0
        x: Math.round((root.width - width) / 2); y: Math.round((root.height - height) / 2)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.66) }
        background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairline; layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.78); shadowBlur: 1.0; shadowVerticalOffset: 18 } }
        contentItem: Column {
            anchors.fill: parent; anchors.margins: 24; spacing: 16
            Row { width: parent.width
                Column { width: parent.width - 42; spacing: 4; Text { text: L10n.t("export.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 25 }
Text { text: Object.keys(root.selectedExportRows).length + " " + L10n.t("export.selected"); color: Theme.textSecondary; font.pixelSize: 12 } }
                PfIconButton { iconSource: "qrc:/qt/qml/PfUi/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: exportDialog.close() }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Text { text: L10n.t("export.format"); color: Theme.textSecondary; font.pixelSize: 11 }
            ComboBox { id: exportFormat; width: parent.width; height: 36; model: ["JSON", "CSV", "TXT", "EDL", "FCPXML", "AEP"] }
            Text { text: L10n.t("export.numbering"); color: Theme.textSecondary; font.pixelSize: 11 }
            Row { width: parent.width; spacing: 8; PfButton { width: (parent.width - 8) / 2; text: L10n.t("export.asVideo"); quiet: exportNumbering.currentIndex !== 0; onClicked: exportNumbering.currentIndex = 0 }
PfButton { width: (parent.width - 8) / 2; text: L10n.t("export.bySort"); quiet: exportNumbering.currentIndex !== 1; onClicked: exportNumbering.currentIndex = 1 } }
            ComboBox { id: exportNumbering; visible: false; model: [0, 1] }
            Text { text: L10n.t("export.cutMode"); color: Theme.textSecondary; font.pixelSize: 11 }
            Row { width: parent.width; spacing: 8; PfButton { width: (parent.width - 8) / 2; text: L10n.t("export.exact"); quiet: cutMode.currentIndex !== 0; onClicked: cutMode.currentIndex = 0 }
PfButton { width: (parent.width - 8) / 2; text: L10n.t("export.fast"); quiet: cutMode.currentIndex !== 1; onClicked: cutMode.currentIndex = 1 } }
            ComboBox { id: cutMode; visible: false; model: [0, 1] }
            Text { width: parent.width; text: L10n.t("export.hint"); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap }
            PfButton { width: parent.width; text: L10n.t("export.prepare"); sageAction: true; onClicked: exportDialog.close() }
        }
    }

    Loader { id: splashLoader; anchors.fill: parent; active: true; sourceComponent: Splash {} }
    Timer { interval: 1100; running: splashLoader.active; onTriggered: splashLoader.active = false }

    Rectangle {
        anchors.fill: parent; color: Theme.canvas
        Column {
            anchors.fill: parent; spacing: 0
            Rectangle {
                width: parent.width; height: Theme.topBarHeight; color: Theme.canvas
                Row { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
                    Text { text: "Parallel Finder"; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 22 }
                    Rectangle { width: 1; height: 22; color: Theme.hairline; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: L10n.t("top.workspace"); color: Theme.textSecondary; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                }
                Row { anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
                    Text { text: Analysis.busy ? L10n.t("top.analyzing") : L10n.t("top.ready"); color: Analysis.busy ? Theme.accent : Theme.textSecondary; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                    Rectangle { width: gpuBadge.implicitWidth + 22; height: 28; radius: 14; color: AppInfo.backendIsGpu ? Theme.sageMuted : Theme.surfaceRaised; border.color: Theme.border; Row { anchors.centerIn: parent; spacing: 7; Rectangle { width: 5; height: 5; radius: 3; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textDisabled; anchors.verticalCenter: parent.verticalCenter }
Text { id: gpuBadge; text: AppInfo.gpuSummary; color: AppInfo.backendIsGpu ? Theme.sageBright : Theme.textSecondary; font.pixelSize: 10 } } }
                    PfIconButton { iconSource: "qrc:/qt/qml/PfUi/assets/settings.svg"; accessibleName: L10n.t("top.settings"); onClicked: settingsDialog.open() }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Row {
                id: workspace
                width: parent.width - 40; height: parent.height - Theme.topBarHeight - 33
                anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 16; spacing: 14

                Rectangle {
                    id: sourceRail; width: root.railWidth; height: parent.height; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
                    layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.48); shadowBlur: 0.75; shadowVerticalOffset: 10 }
                    Flickable {
                        id: sourceScroll; anchors.fill: parent; anchors.margins: 16; clip: true; contentWidth: width; contentHeight: sourceContent.implicitHeight + 18; boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        Column {
                            id: sourceContent; width: sourceScroll.width; spacing: 10
                            Row { width: parent.width; Column { width: parent.width - 34; spacing: 4; Text { text: L10n.t("sources.title"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
Text { text: root.sourceFiles.length > 0 ? root.sourceFiles.length + " " + L10n.t("sources.loaded") : L10n.t("sources.subtitle"); color: Theme.textSecondary; font.pixelSize: 11 } }
Text { text: root.sourceFiles.length; color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 19 } }
                            DropArea {
                                width: parent.width; height: 102
                                onDropped: if (drop.hasUrls) root.addFiles(drop.urls)
                                Rectangle { anchors.fill: parent; radius: Theme.radiusButton; color: parent.containsDrag ? Theme.accentMuted : Theme.well; border.width: 1; border.color: parent.containsDrag ? Theme.accent : Theme.hairlineStrong
                                    Column { anchors.centerIn: parent; spacing: 7; Text { anchors.horizontalCenter: parent.horizontalCenter; text: "+"; color: Theme.accent; font.pixelSize: 23 }
Text { anchors.horizontalCenter: parent.horizontalCenter; text: L10n.t("sources.dropTitle"); color: Theme.textPrimary; font.pixelSize: 12 }
Text { anchors.horizontalCenter: parent.horizontalCenter; text: L10n.t("sources.dropHint"); color: Theme.textSecondary; font.pixelSize: 10 } }
                                }
                            }
                            Row { width: parent.width; spacing: 7; PfButton { width: (parent.width - 7) / 2; text: L10n.t("sources.add"); onClicked: fileDialog.open() }
PfButton { width: (parent.width - 7) / 2; text: L10n.t("sources.folder"); quiet: true; onClicked: folderDialog.open() } }
                            Row { width: parent.width; spacing: 7; PfButton { width: (parent.width - 7) / 2; text: L10n.t("sources.clear"); quiet: true; enabled: root.sourceFiles.length > 0; onClicked: { root.sourceFiles = []; root.sourceFilesChanged(); root.clearResultsSelection() } }
PfButton { width: (parent.width - 7) / 2; text: L10n.t("sources.remove"); quiet: true; enabled: false } }
                            ListView {
                                width: parent.width; height: root.sourceFiles.length > 0 ? Math.min(128, root.sourceFiles.length * 30) : 34; clip: true; model: root.sourceFiles
                                delegate: Rectangle { width: ListView.view.width; height: 30; color: index % 2 === 0 ? Theme.surfaceRaised : "transparent"; radius: 5; Text { anchors.left: parent.left; anchors.leftMargin: 9; anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; text: (index + 1) + "  " + root.sourceName(modelData); color: Theme.textSecondary; font.pixelSize: 10; elide: Text.ElideMiddle } }
                                Text { anchors.centerIn: parent; visible: root.sourceFiles.length === 0; text: L10n.t("sources.empty"); color: Theme.textDisabled; font.pixelSize: 10 }
                            }
                            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                            Text { text: L10n.t("search.similarityGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                            Column { width: parent.width; spacing: 3
                                Row { width: parent.width; Text { text: L10n.t("search.similarity"); color: Theme.textSecondary; font.pixelSize: 11 }
Item { width: parent.width - 100; height: 1 }
Text { text: Math.round(Analysis.similarityThreshold * 100) + "%"; color: Theme.accent; font.pixelSize: 11 } }
                                PfSlider { width: parent.width; from: 0.5; to: 0.99; stepSize: 0.01; value: Analysis.similarityThreshold; onValueChanged: if (Math.abs(Analysis.similarityThreshold - value) > 0.001) Analysis.similarityThreshold = value }
                                Text { text: L10n.t("search.candidate"); color: Theme.textSecondary; font.pixelSize: 11 }
                                PfSlider { width: parent.width; from: 0.1; to: 0.95; stepSize: 0.01; value: Analysis.candidateThreshold; onValueChanged: if (Math.abs(Analysis.candidateThreshold - value) > 0.001) Analysis.candidateThreshold = value }
                            }
                            Text { text: L10n.t("search.spacingGroup"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                            Column { width: parent.width; spacing: 3
                                Text { text: L10n.t("search.repeatGap") + "  ·  " + Analysis.repeatGap.toFixed(1) + " с"; color: Theme.textSecondary; font.pixelSize: 11 }
PfSlider { width: parent.width; from: 0; to: 30; stepSize: 0.5; value: Analysis.repeatGap; onValueChanged: if (Math.abs(Analysis.repeatGap - value) > 0.001) Analysis.repeatGap = value }
                                Text { text: L10n.t("search.sameFileGap") + "  ·  " + Analysis.sameFileGap.toFixed(1) + " с"; color: Theme.textSecondary; font.pixelSize: 11 }
PfSlider { width: parent.width; from: 0; to: 15; stepSize: 0.5; value: Analysis.sameFileGap; onValueChanged: if (Math.abs(Analysis.sameFileGap - value) > 0.001) Analysis.sameFileGap = value }
                                Text { text: L10n.t("search.crossFileGap") + "  ·  " + Analysis.crossFileGap.toFixed(1) + " с"; color: Theme.textSecondary; font.pixelSize: 11 }
PfSlider { width: parent.width; from: 0; to: 15; stepSize: 0.5; value: Analysis.crossFileGap; onValueChanged: if (Math.abs(Analysis.crossFileGap - value) > 0.001) Analysis.crossFileGap = value }
                                Text { text: L10n.t("search.duplicateWindow") + "  ·  " + Analysis.duplicateWindow.toFixed(1) + " с"; color: Theme.textSecondary; font.pixelSize: 11 }
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
                            Row { width: parent.width; Text { text: L10n.t("search.quality"); color: Theme.textSecondary; font.pixelSize: 11 }
Item { width: parent.width - 92; height: 1 }
Text { text: L10n.t("search.maximum"); color: Theme.sage; font.pixelSize: 10 } }
                            Row { width: parent.width; spacing: 5; PfButton { width: (parent.width - 10) / 3; text: L10n.t("search.fast"); quiet: true }
PfButton { width: (parent.width - 10) / 3; text: L10n.t("search.medium"); quiet: true }
PfButton { width: (parent.width - 10) / 3; text: L10n.t("search.maximum"); sageAction: true } }
                            PfCheckBox { text: L10n.t("search.normalize"); checked: true }
                            PfCheckBox { text: L10n.t("search.mirror"); checked: true }
                            PfButton { width: parent.width; text: Analysis.busy ? L10n.t("search.running") : L10n.t("search.start"); enabled: root.sourceFiles.length > 0 && !Analysis.busy; onClicked: Analysis.analyzeFiles(root.sourceFiles) }
                            Text { width: parent.width; text: Analysis.status; color: Theme.textDisabled; font.pixelSize: 10; wrapMode: Text.WordWrap }
                        }
                    }
                }

                Column {
                    id: centerColumn; width: parent.width - root.railWidth * 2 - 28; height: parent.height; spacing: 12
                    Rectangle {
                        width: parent.width; height: 68; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
                        Row { anchors.fill: parent; anchors.margins: 1
                            Repeater {
                                model: [
                                    { label: L10n.t("stats.files"), value: Analysis.fileCount },
                                    { label: L10n.t("stats.frames"), value: Analysis.frameCount },
                                    { label: L10n.t("stats.scenes"), value: Analysis.sceneCount },
                                    { label: L10n.t("stats.pairs"), value: Analysis.matchCount },
                                    { label: L10n.t("stats.duration"), value: Math.round(Analysis.durationSeconds) + " с" },
                                    { label: L10n.t("stats.status"), value: Analysis.busy ? "…" : "—" }
                                ]
                                delegate: Item { width: parent.width / 6; height: parent.height; Rectangle { visible: index > 0; x: 0; y: 16; width: 1; height: parent.height - 32; color: Theme.hairline }
Column { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 4; Text { text: modelData.label; color: Theme.textDisabled; font.pixelSize: 10 }
Text { text: modelData.value; color: index === 3 ? Theme.accent : Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 21 } } }
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width; height: parent.height - 80; color: Theme.panel; radius: Theme.radiusCard; border.color: Theme.border
                        layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.50); shadowBlur: 0.72; shadowVerticalOffset: 12 }
                        Column { anchors.fill: parent; anchors.margins: 16; spacing: 11
                            Row { width: parent.width; height: 23; Text { text: L10n.t("center.comparison"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
Item { width: parent.width - 300; height: 1 }
Text { text: Analysis.busy ? L10n.t("center.processing") : (root.selectedResultIndex >= 0 ? L10n.t("center.selected") : L10n.t("center.waiting")); color: Theme.textSecondary; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } }
                            Rectangle { width: parent.width; height: 4; radius: 2; color: Theme.well; Rectangle { width: Analysis.busy ? parent.width * 0.42 : Analysis.fileCount > 0 ? parent.width : 0; height: parent.height; radius: 2; color: Theme.accent; Behavior on width { NumberAnimation { duration: 350 } } } }
                            Rectangle {
                                id: comparisonStage; width: parent.width; height: parent.height - 102; color: Theme.well; radius: Theme.radiusButton; border.color: Theme.hairline
                                Column { anchors.fill: parent; anchors.margins: 16; spacing: 10; visible: root.selectedResultIndex < 0
                                    Row { width: parent.width; Text { text: L10n.t("center.motionField"); color: Theme.textDisabled; font.pixelSize: 10; font.letterSpacing: 0.8 }
Item { width: parent.width - 260; height: 1 }
Text { text: Analysis.fileCount > 0 ? Analysis.poseDetectionCount + " " + L10n.t("center.poseCount") : L10n.t("center.noMaterial"); color: Theme.textSecondary; font.pixelSize: 10 } }
                                    Item { width: parent.width; height: parent.height - 72
                                        Rectangle { anchors.centerIn: parent; width: 260; height: 160; radius: 80; color: Theme.glowA; opacity: 0.10; layer.enabled: true; layer.effect: MultiEffect { blurEnabled: true; blur: 1.0 } }
                                        Canvas { id: motionField; anchors.centerIn: parent; width: Math.min(520, parent.width - 40); height: 190
                                            onPaint: {
                                                const ctx = getContext("2d"); ctx.clearRect(0, 0, width, height); const cy = height * 0.54; ctx.lineCap = "round"
                                                ctx.lineWidth = 8; ctx.globalAlpha = 0.05; ctx.strokeStyle = Theme.accent; ctx.beginPath(); ctx.moveTo(26, cy + 10); ctx.bezierCurveTo(width * 0.30, 20, width * 0.64, height - 6, width - 24, 46); ctx.stroke()
                                                ctx.lineWidth = 1; ctx.globalAlpha = 0.76; ctx.strokeStyle = Theme.accent; ctx.beginPath(); ctx.moveTo(26, cy + 10); ctx.bezierCurveTo(width * 0.30, 20, width * 0.64, height - 6, width - 24, 46); ctx.stroke()
                                                ctx.lineWidth = 8; ctx.globalAlpha = 0.045; ctx.strokeStyle = Theme.sage; ctx.beginPath(); ctx.moveTo(40, 44); ctx.bezierCurveTo(width * 0.34, height - 8, width * 0.68, 30, width - 36, cy + 18); ctx.stroke()
                                                ctx.lineWidth = 1; ctx.globalAlpha = 0.68; ctx.strokeStyle = Theme.sage; ctx.beginPath(); ctx.moveTo(40, 44); ctx.bezierCurveTo(width * 0.34, height - 8, width * 0.68, 30, width - 36, cy + 18); ctx.stroke()
                                                ctx.globalAlpha = 0.95; ctx.fillStyle = Theme.accent; ctx.beginPath(); ctx.arc(26, cy + 10, 3, 0, Math.PI * 2); ctx.fill(); ctx.fillStyle = Theme.sage; ctx.beginPath(); ctx.arc(width - 36, cy + 18, 3, 0, Math.PI * 2); ctx.fill()
                                            }
                                        }
                                        Column { anchors.centerIn: parent; spacing: 9; Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sourceFiles.length > 0 ? L10n.t("center.readyTitle") : L10n.t("center.emptyTitle"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 20 }
Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sourceFiles.length > 0 ? L10n.t("center.readyHint") : L10n.t("center.emptyHint"); color: Theme.textSecondary; font.pixelSize: 12 }
PfButton { anchors.horizontalCenter: parent.horizontalCenter; visible: root.sourceFiles.length === 0; text: L10n.t("sources.add"); onClicked: fileDialog.open() } }
                                    }
                                }
                                Row { anchors.fill: parent; anchors.margins: 12; spacing: 10; visible: root.selectedResultIndex >= 0
                                    Rectangle { width: (parent.width - 10) / 2; height: parent.height; color: Theme.canvas; radius: Theme.radiusButton; border.color: Qt.rgba(0.85, 0.47, 0.34, 0.40); Column { anchors.fill: parent; anchors.margins: 10; spacing: 8; Row { width: parent.width; Text { text: "A"; color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 17 }
Item { width: parent.width - 38; height: 1 }
Text { text: "00:00:00"; color: Theme.textSecondary; font.pixelSize: 10 } }
Rectangle { width: parent.width; height: parent.height - 42; color: Theme.well; radius: 6; Image { id: previewA; anchors.fill: parent; source: root.selectedResultIndex >= 0 && Analysis.previewA.length > root.selectedResultIndex ? Analysis.previewA[root.selectedResultIndex] : ""; fillMode: Image.PreserveAspectCrop; smooth: true; visible: source.length > 0 }
Text { anchors.centerIn: parent; visible: previewA.source.length === 0; text: "A"; color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 42 } } } }
                                    Rectangle { width: (parent.width - 10) / 2; height: parent.height; color: Theme.canvas; radius: Theme.radiusButton; border.color: Qt.rgba(0.49, 0.60, 0.52, 0.52); Column { anchors.fill: parent; anchors.margins: 10; spacing: 8; Row { width: parent.width; Text { text: "B"; color: Theme.sage; font.family: Theme.displayFont; font.pixelSize: 17 }
Item { width: parent.width - 38; height: 1 }
Text { text: "00:00:00"; color: Theme.textSecondary; font.pixelSize: 10 } }
Rectangle { width: parent.width; height: parent.height - 42; color: Theme.well; radius: 6; Image { id: previewB; anchors.fill: parent; source: root.selectedResultIndex >= 0 && Analysis.previewB.length > root.selectedResultIndex ? Analysis.previewB[root.selectedResultIndex] : ""; fillMode: Image.PreserveAspectCrop; smooth: true; visible: source.length > 0 }
Text { anchors.centerIn: parent; visible: previewB.source.length === 0; text: "B"; color: Theme.sage; font.family: Theme.displayFont; font.pixelSize: 42 } } } }
                                }
                            }
                            Row { width: parent.width; height: 20; Text { text: L10n.t("timeline.title"); color: Theme.textDisabled; font.pixelSize: 10; font.letterSpacing: 0.8 }
Item { width: parent.width - 160; height: 1 }
PfIconButton { width: 24; height: 24; iconSize: 14; iconSource: "qrc:/qt/qml/PfUi/assets/play.svg"; accessibleName: L10n.t("timeline.play") }
Text { text: Analysis.durationSeconds > 0 ? Math.round(Analysis.durationSeconds) + " с" : "—"; color: Theme.textSecondary; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } }
                            Rectangle { width: parent.width; height: 34; color: Theme.surfaceRaised; radius: 5; border.color: Theme.border; Rectangle { x: 8; y: 9; width: parent.width * 0.18; height: 16; radius: 3; color: Theme.sageMuted }
Repeater { model: [0.29, 0.37, 0.63, 0.71]; delegate: Rectangle { x: parent.width * modelData; y: 5; width: 2; height: 24; color: index < 2 ? Theme.accent : Theme.sage; visible: root.selectedResultIndex >= 0 } } }
                        }
                    }
                }

                Rectangle {
                    id: resultsRail; width: root.railWidth; height: parent.height; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
                    layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.48); shadowBlur: 0.75; shadowVerticalOffset: 10 }
                    Column { anchors.fill: parent; anchors.margins: 16; spacing: 12
                        Row { width: parent.width; Column { width: parent.width - 36; spacing: 4; Text { text: L10n.t("results.title"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
Text { text: Analysis.matchCount > 0 ? Analysis.matchCount + " " + L10n.t("results.found") : L10n.t("results.emptyHint"); color: Theme.textSecondary; font.pixelSize: 11 } }
Text { text: Analysis.matchCount; color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 20 } }
                        Row { width: parent.width; spacing: 6; PfIconButton { width: 28; height: 28; iconSource: "qrc:/qt/qml/PfUi/assets/chevron-left.svg"; accessibleName: L10n.t("results.previous"); enabled: root.selectedResultIndex > 0; onClicked: root.selectedResultIndex-- }
Text { width: parent.width - 68; text: root.selectedResultIndex >= 0 ? (root.selectedResultIndex + 1) + " / " + Analysis.matchCount : "—"; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
PfIconButton { width: 28; height: 28; iconSource: "qrc:/qt/qml/PfUi/assets/chevron-right.svg"; accessibleName: L10n.t("results.next"); enabled: root.selectedResultIndex >= 0 && root.selectedResultIndex < Analysis.matchCount - 1; onClicked: root.selectedResultIndex++ } }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Row { width: parent.width; spacing: 6; PfButton { width: (parent.width - 6) / 2; text: L10n.t("results.sort"); quiet: true }
PfButton { width: (parent.width - 6) / 2; text: L10n.t("results.export"); enabled: root.hasSelectedExport; quiet: !root.hasSelectedExport; onClicked: exportDialog.open() } }
                        Text { text: L10n.t("results.filters"); color: Theme.textSecondary; font.pixelSize: 11 }
                        Row { width: parent.width; spacing: 5; PfButton { width: (parent.width - 10) / 3; text: L10n.t("results.all"); quiet: root.activeMovementFilter !== "all"; onClicked: root.activeMovementFilter = "all" }
PfButton { width: (parent.width - 10) / 3; text: L10n.t("results.forward"); quiet: root.activeMovementFilter !== "forward"; onClicked: root.activeMovementFilter = "forward" }
PfButton { width: (parent.width - 10) / 3; text: L10n.t("results.side"); quiet: root.activeMovementFilter !== "side"; onClicked: root.activeMovementFilter = "side" } }
                        ListView {
                            id: resultList; width: parent.width; height: parent.height - 178; clip: true; spacing: 5; model: Analysis.resultItems
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                width: resultList.width - 8; height: Theme.resultRowHeight + 28; radius: 7; color: index === root.selectedResultIndex ? Theme.accentMuted : Theme.surfaceRaised; border.color: index === root.selectedResultIndex ? Qt.rgba(0.85, 0.47, 0.34, 0.40) : Theme.border
                                property bool exportChecked: root.selectedExportRows[index] === true
                                Row { anchors.fill: parent; anchors.margins: 8; spacing: 8
                                    CheckBox { id: exportCheck; width: 18; height: 18; checked: parent.parent.exportChecked; onToggled: { const next = Object.assign({}, root.selectedExportRows); if (checked) next[index] = true; else delete next[index]; root.selectedExportRows = next }
                                        indicator: Rectangle { anchors.centerIn: parent; width: 16; height: 16; radius: 4; color: exportCheck.checked ? Theme.accent : Theme.well; border.color: exportCheck.checked ? Theme.accent : Theme.hairlineStrong; Text { anchors.centerIn: parent; text: "✓"; visible: exportCheck.checked; color: Theme.canvas; font.pixelSize: 11; font.weight: Font.DemiBold } }
contentItem: Item {} }
                                    Column { width: parent.width - 32; spacing: 3; Text { width: parent.width; text: modelData; color: Theme.textPrimary; font.pixelSize: 10; elide: Text.ElideRight }
Row { width: parent.width; spacing: 8; Text { text: "A / B"; color: index === root.selectedResultIndex ? Theme.accent : Theme.textDisabled; font.pixelSize: 9 }
Text { text: L10n.t("results.selectPair"); color: Theme.textDisabled; font.pixelSize: 9 } } }
                                }
                                MouseArea { anchors.fill: parent; z: -1; onClicked: root.selectedResultIndex = index }
                            }
                            Text { anchors.centerIn: parent; visible: Analysis.matchCount === 0; width: parent.width - 28; text: L10n.t("results.emptyBody"); color: Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
    }
}
