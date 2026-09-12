import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
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
    property string activeMovementFilter: "all"
    property real railWidth: Math.max(272, Math.min(300, width * 0.22))
    property var selectedRecord: null

    function addFiles(urls) {
        for (const url of urls || []) {
            const text = url.toString()
            let path = text
            if (text.toLowerCase().startsWith("file:")) {
                path = url.toLocalFile ? url.toLocalFile() : decodeURIComponent(text.replace(/^file:\/\//, ""))
            }
            if (path.match(/^\/[A-Za-z]:/)) path = path.slice(1)
            if (root.sourceFiles.indexOf(path) < 0) root.sourceFiles.push(path)
        }
        root.sourceFilesChanged()
        Analysis.inspectFiles(root.sourceFiles)
    }
    function addFolder(url) {
        root.addFiles(Analysis.filesInFolder(url.toString()))
    }
    function removeSource(index) {
        if (index < 0 || index >= root.sourceFiles.length) return
        const next = root.sourceFiles.slice(); next.splice(index, 1); root.sourceFiles = next
        Analysis.inspectFiles(root.sourceFiles)
    }
    function resetMatcherSettings() {
        Analysis.similarityThreshold = 0.85; Analysis.candidateThreshold = 0.55; Analysis.repeatGap = 6.0
        Analysis.sameFileGap = 2.0; Analysis.crossFileGap = 0.0; Analysis.duplicateWindow = 1.5
        Analysis.noiseFactor = 1.0; Analysis.maxUniqueResults = 100; Analysis.timeWeight = 0.25
    }
    function selectResult(index) {
        root.selectedResultIndex = index
        root.selectedRecord = index >= 0 && index < Analysis.results.length ? Analysis.results[index] : null
    }
    function clearResultsSelection() { root.selectedExportRows = ({}); root.selectResult(-1) }

    FileDialog { id: fileDialog; title: L10n.t("dialog.chooseVideos"); fileMode: FileDialog.OpenFiles; nameFilters: [L10n.t("dialog.videoFilter"), L10n.t("dialog.allFiles")]; onAccepted: root.addFiles(selectedFiles) }
    FolderDialog { id: folderDialog; title: L10n.t("dialog.chooseFolder"); onAccepted: root.addFolder(selectedFolder) }
    SettingsDialog { id: settingsDialog; rootWindow: root; onResetRequested: root.resetMatcherSettings() }
    ExportDialog { id: exportDialog; rootWindow: root; selectedRows: root.selectedExportRows }
    Loader { id: splashLoader; anchors.fill: parent; active: true; sourceComponent: Splash {} }
    Timer { interval: 1100; running: splashLoader.active; onTriggered: splashLoader.active = false }

    Connections { target: Analysis; function onResultsChanged() { root.clearResultsSelection() } }

    Rectangle { anchors.fill: parent; color: Theme.canvas
        Column { anchors.fill: parent; spacing: 0
            TopBar { width: parent.width; height: Theme.topBarHeight; busy: Analysis.busy; onSettingsRequested: settingsDialog.open() }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Row {
                id: workspace
                width: parent.width - 40; height: parent.height - Theme.topBarHeight - 33
                anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 16; spacing: 14
                SourcesRail {
                    id: sourcesRail; width: root.railWidth; height: parent.height; sourceFiles: root.sourceFiles
                    onFilesRequested: function(urls) { if (urls && urls.length > 0) root.addFiles(urls); else fileDialog.open() }
                    onFolderRequested: folderDialog.open()
                    onClearRequested: { root.sourceFiles = []; root.clearResultsSelection(); Analysis.inspectFiles([]) }
                    onRemoveRequested: root.removeSource(index)
                    onAnalyzeRequested: Analysis.analyzeFiles(root.sourceFiles)
                }
                MotionCenter {
                    id: motionCenter; width: parent.width - root.railWidth * 2 - 28; height: parent.height
                    sourceFiles: root.sourceFiles; selectedRecord: root.selectedRecord; onAddRequested: fileDialog.open()
                }
                ResultsRail {
                    id: resultsRail; width: root.railWidth; height: parent.height
                    results: Analysis.results; activeFilter: root.activeMovementFilter; selectedRows: root.selectedExportRows; selectedIndex: root.selectedResultIndex
                    onActiveFilterChanged: root.activeMovementFilter = activeFilter
                    onExportSelectionChanged: root.selectedExportRows = rows
                    onResultSelected: root.selectResult(index)
                    onExportRequested: exportDialog.open()
                }
            }
        }
    }
}
