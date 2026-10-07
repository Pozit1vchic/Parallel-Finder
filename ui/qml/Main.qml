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
    property bool startupPresented: true
    visible: typeof pfDeferWindowPresentation === "undefined" || !pfDeferWindowPresentation
    title: L10n.t("app.title") + " · " + AppInfo.version
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
    UpdateDialog { id: updateDialog; rootWindow: root; presentationBlocked: settingsDialog.visible || exportDialog.visible }

    Connections {
        target: Analysis
        function onResultsChanged() { root.syncResultsSelection() }
        function onResultCategoriesChanged() { root.selectResult(root.selectedResultIndex) }
    }

    function resultKeysEnabled() {
        if (settingsDialog.visible || exportDialog.visible || updateDialog.visible || fileDialog.visible
                || folderDialog.visible || root.selectedRecord === null) return false
        for (let item = root.activeFocusItem; item; item = item.parent) {
            // Analyze/add buttons retain focus after a run. Do not disable
            // result navigation for the entire source panel because of that.
            if (item.cursorPosition !== undefined || item instanceof ComboBox || item instanceof Slider) return false
        }
        return true
    }
    Shortcut { sequence: "Up"; enabled: root.resultKeysEnabled(); onActivated: resultsRail.selectPrevious() }
    Shortcut { sequence: "Down"; enabled: root.resultKeysEnabled(); onActivated: resultsRail.selectNext() }

    Rectangle { id: workspaceSurface; objectName: "workspaceSurface"; anchors.fill: parent; color: Theme.canvas
        PfReveal { objectName: "workspaceReveal"; anchors.fill: parent; active: root.startupPresented; distance: 24
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
                    sourceFiles: root.sourceFiles; selectedRecord: root.selectedRecord
                    selectedSource: sourcesRail.selectedSourceIndex >= 0 ? String(root.sourceFiles[sourcesRail.selectedSourceIndex] || "") : ""
                    onAddRequested: fileDialog.open(); onAnalyzeRequested: Analysis.analyzeFiles(root.sourceFiles)
                }
                ResultsRail {
                    id: resultsRail
                    Layout.minimumWidth: 246
                    Layout.preferredWidth: Theme.sidePanelWidth
                    Layout.maximumWidth: 310
                    Layout.fillHeight: true
                    results: { Analysis.resultCategoryRevision; return Analysis.results }
                    selectedRows: root.selectedExportRows; selectedIndex: root.selectedResultIndex
                    onExportSelectionChanged: function(rows) { root.selectedExportRows = rows }
                    onResultSelected: function(index) { root.selectResult(index) }
                    onPairColorRequested: function(index, name, color) { Analysis.setResultCategory(index, name, color) }
                    onExportRequested: function(rows) { exportDialog.selectionOrder = resultsRail.visibleResults.map(function(item) { return Number(item.id) }); exportDialog.selectedRows = rows; exportDialog.open() }
                }
                }
            }
        }
        }
    }
    // Keep the live workspace at its native resolution. Only the cached
    // backdrop fades, so hairlines never switch between two rasterizations.
    ShaderEffectSource {
        id: backdropSnapshot
        objectName: "modalBackdropSnapshot"
        anchors.fill: parent
        sourceItem: modalBackdrop.visible ? workspaceSurface : null
        live: false
        hideSource: false
        visible: false
        textureSize: Qt.size(root.width, root.height)
    }
    MultiEffect {
        id: modalBackdrop
        objectName: "modalBackdrop"
        anchors.fill: parent
        property bool active: (settingsDialog.visible && !settingsDialog.backdropClosing)
            || (exportDialog.visible && !exportDialog.backdropClosing)
            || (updateDialog.visible && !updateDialog.backdropClosing)
        property bool warming: !root.startupPresented
        property bool prepared: true
        source: backdropSnapshot
        blurEnabled: true
        blurMax: 12
        blur: 1
        colorization: 0.6
        colorizationColor: "black"
        opacity: warming ? 1 : active ? 1 : 0
        visible: opacity > 0 && GraphicsInfo.api !== GraphicsInfo.Software
        // Cache the finished blur; transition frames composite one texture.
        layer.enabled: visible
        onActiveChanged: if (active) backdropSnapshot.scheduleUpdate()
        onWarmingChanged: {
            if (warming) { prepared = false; backdropSnapshot.scheduleUpdate() }
            else Qt.callLater(function() { modalBackdrop.prepared = true })
        }
        Behavior on opacity {
            enabled: modalBackdrop.prepared && !Theme.reducedMotion
            NumberAnimation { duration: modalBackdrop.active ? Theme.motionRevealDuration : Theme.motionChangeDuration; easing.type: Easing.OutCubic }
        }
    }
    Connections {
        target: root
        function onWidthChanged() { if (modalBackdrop.active) backdropSnapshot.scheduleUpdate() }
        function onHeightChanged() { if (modalBackdrop.active) backdropSnapshot.scheduleUpdate() }
    }

}
