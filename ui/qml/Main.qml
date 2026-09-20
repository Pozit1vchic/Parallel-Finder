import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
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
    property var selectedRecord: null

    function addFiles(urls) {
        const next = root.sourceFiles.slice()
        for (const url of urls || []) {
            const text = url.toString()
            let path = text
            if (text.toLowerCase().startsWith("file:")) {
                path = url.toLocalFile ? url.toLocalFile() : decodeURIComponent(text.replace(/^file:\/\//, ""))
            }
            if (path.match(/^\/[A-Za-z]:/)) path = path.slice(1)
            if (next.indexOf(path) < 0) next.push(path)
        }
        root.sourceFiles = next
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
    function selectResult(index) {
        const wanted = Number(index)
        const match = (Analysis.results || []).find(function (item) { return Number(item.id) === wanted })
        root.selectedResultIndex = match ? Number(match.id) : -1
        root.selectedRecord = match || null
    }
    function syncResultsSelection() {
        root.selectedExportRows = ({})
        if (Analysis.results.length > 0) root.selectResult(Number(Analysis.results[0].id))
        else root.selectResult(-1)
    }

    FileDialog { id: fileDialog; title: L10n.t("dialog.chooseVideos"); fileMode: FileDialog.OpenFiles; nameFilters: [L10n.t("dialog.videoFilter"), L10n.t("dialog.allFiles")]; onAccepted: root.addFiles(selectedFiles) }
    FolderDialog { id: folderDialog; title: L10n.t("dialog.chooseFolder"); onAccepted: root.addFolder(selectedFolder) }
    SettingsDialog {
        id: settingsDialog
        rootWindow: root
    }
    ExportDialog { id: exportDialog; rootWindow: root; selectedRows: root.selectedExportRows }

    Connections { target: Analysis; function onResultsChanged() { root.syncResultsSelection() } }

    function resultKeysEnabled() {
        if (settingsDialog.visible || exportDialog.visible || root.selectedRecord === null) return false
        for (let item = root.activeFocusItem; item; item = item.parent) {
            if (item === sourcesRail) return false
            if (item.cursorPosition !== undefined || item instanceof ComboBox || item instanceof Slider) return false
        }
        return true
    }
    Shortcut { sequence: "Up"; enabled: root.resultKeysEnabled(); onActivated: resultsRail.selectPrevious() }
    Shortcut { sequence: "Down"; enabled: root.resultKeysEnabled(); onActivated: resultsRail.selectNext() }

    Rectangle { id: workspaceSurface; anchors.fill: parent; color: Theme.canvas
        layer.enabled: (settingsDialog.visible || exportDialog.visible) && GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect { blurEnabled: true; blurMax: 12; blur: 1.0; colorization: 0.6; colorizationColor: "black" }
        ColumnLayout { anchors.fill: parent; spacing: 0
            TopBar { Layout.fillWidth: true; Layout.preferredHeight: Theme.topBarHeight; Layout.minimumHeight: Theme.topBarHeight; busy: Analysis.busy; onSettingsRequested: settingsDialog.open() }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline }
            Item {
                id: workspace
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                Layout.minimumHeight: 260
                RowLayout {
                    anchors.fill: parent
                    spacing: 14
                SourcesRail {
                    id: sourcesRail
                    Layout.minimumWidth: 246
                    Layout.preferredWidth: Theme.sidePanelWidth
                    Layout.maximumWidth: 310
                    Layout.fillHeight: true
                    sourceFiles: root.sourceFiles
                    onFilesRequested: function(urls) { if (urls && urls.length > 0) root.addFiles(urls); else fileDialog.open() }
                    onFolderRequested: folderDialog.open()
                    onClearRequested: { root.sourceFiles = []; root.syncResultsSelection(); Analysis.inspectFiles([]) }
                    onRemoveRequested: root.removeSource(index)
                    onAnalyzeRequested: Analysis.analyzeFiles(root.sourceFiles)
                }
                MotionCenter {
                    id: motionCenter
                    Layout.minimumWidth: 420
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sourceFiles: root.sourceFiles; selectedRecord: root.selectedRecord; onAddRequested: fileDialog.open(); onAnalyzeRequested: Analysis.analyzeFiles(root.sourceFiles)
                }
                ResultsRail {
                    id: resultsRail
                    Layout.minimumWidth: 246
                    Layout.preferredWidth: Theme.sidePanelWidth
                    Layout.maximumWidth: 310
                    Layout.fillHeight: true
                    results: Analysis.results; selectedRows: root.selectedExportRows; selectedIndex: root.selectedResultIndex
                    onExportSelectionChanged: root.selectedExportRows = rows
                    onResultSelected: root.selectResult(index)
                    onExportRequested: exportDialog.open()
                }
                }
            }
        }
    }
}
