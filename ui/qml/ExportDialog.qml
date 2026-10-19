import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtCore
import PfUi
import PfUiBridge

Popup {
    id: root
    objectName: "exportDialog"

    property bool backdropClosing: false
    property bool userPositioned: false
    property bool baseNameCustomized: false
    property real entranceOffset: 0
    property var rootWindow
    property var selectedRows: ({})
    property var selectionOrder: []
    property string outputFolder: exportPreferences.lastOutputFolder
    property string exportStatus: ""
    property var results: { Analysis.resultCategoryRevision; return Analysis.results }
    property var classOrder: []

    property int qualityChoice: 1 // lossless, high, custom
    readonly property bool losslessExport: qualityChoice === 0
    readonly property bool customQuality: qualityChoice === 2
    readonly property bool mergeChronological: selectedFormat === "FFMPEG" && mergeCheck.checked
    readonly property string selectedFormat: ["FFMPEG", "JSON", "CSV", "TXT"][exportFormat.currentIndex] || "FFMPEG"
    readonly property string fileBaseName: prefixField.text.trim()
    readonly property string selectedContainer: containerMode.currentIndex >= 0
        ? ["mp4", "mov", "mkv", "webm"][containerMode.currentIndex] : "mp4"
    readonly property int selectedCutMode: losslessExport ? 2 : 0
    readonly property int encodingQuality: 18
    readonly property int videoBitrateKbps: customQuality
        ? Math.round(Number(bitrateField.text.replace(",", ".")) * 1000) : 0
    readonly property int maxWidth: customQuality ? parsedDimension(widthField.text) : 0
    readonly property int maxHeight: customQuality ? parsedDimension(heightField.text) : 0
    readonly property bool qualityValid: !customQuality
        || (isFinite(videoBitrateKbps) && videoBitrateKbps >= 1000 && videoBitrateKbps <= 500000
            && maxWidth >= 0 && maxHeight >= 0)

    function defaultBaseName() { return mergeCheck.checked ? "frame_combined" : "frame" }
    function parsedDimension(value) {
        const raw = String(value || "").trim()
        if (raw.length === 0) return 0
        const parsed = Number(raw)
        return Number.isFinite(parsed) && parsed >= 2 && parsed <= 32768 ? Math.round(parsed) : -1
    }
    function restoreQualityChoice() { qualityMode.currentIndex = qualityChoice }
    function refreshClasses() {
        const present = []
        for (const item of root.results) {
            if (root.selectedRows[item.id] !== true || item.hidden) continue
            const classification = L10n.movementKey(item.classification)
            if (present.indexOf(classification) < 0) present.push(classification)
        }
        function rank(key) {
            return Math.min.apply(null, key.split("+").map(function(action) {
                const index = L10n.movementTypes.indexOf(action)
                return index < 0 ? L10n.movementTypes.length : index
            }))
        }
        classOrder = present.sort(function(a, b) {
            return rank(a) - rank(b)
                || a.split("+").length - b.split("+").length
                || a.localeCompare(b)
        })
    }
    function selectedIndexes() {
        return root.results
            .filter(function(row) { return root.selectedRows[row.id] === true && !row.hidden })
            .map(function(row) { return Number(row.id) })
    }
    function localFolderPath(value) {
        if (value && value.toLocalFile) {
            const local = value.toLocalFile()
            if (local && local.length > 0) return local
        }
        let text = String(value || "")
        if (text.toLowerCase().startsWith("file:")) {
            try { text = decodeURIComponent(text.replace(/^file:\/\//i, "")) }
            catch (error) { text = text.replace(/^file:\/\//i, "") }
        }
        if (/^\/[A-Za-z]:/.test(text)) text = text.slice(1)
        return text
    }
    function rememberFolder(value) {
        const folder = localFolderPath(value).trim()
        outputFolder = folder
        if (folder.length > 0) exportPreferences.lastOutputFolder = folder
    }
    function chooseFolder() {
        if (root.outputFolder.length > 0)
            folderDialog.currentFolder = "file:///" + root.outputFolder.replace(/\\/g, "/")
        folderDialog.open()
    }
    function setHighQuality() {
        qualityChoice = 1
        qualityMode.currentIndex = 1
    }
    function startExport() {
        root.exportStatus = ""
        // Always use exact boundaries and the classifier's stable type order.
        // Lossless is an exact MOV export; ordinary/custom quality re-encodes.
        Analysis.exportResults(
            root.selectedFormat,
            0,
            root.selectedCutMode,
            root.outputFolder,
            root.fileBaseName,
            root.selectedIndexes(),
            root.mergeChronological,
            root.classOrder,
            root.encodingQuality,
            root.videoBitrateKbps,
            false,
            signTransitionsCheck.checked,
            root.selectedContainer,
            root.maxWidth,
            root.maxHeight)
    }

    readonly property string runningLabel: Analysis.exportStage === "preparing"
        ? (L10n.t("ui.323"))
        : Analysis.exportStage === "finalizing"
            ? (L10n.t("ui.324"))
            : L10n.t("export.running")

    Settings {
        id: exportPreferences
        category: "Export"
        property string lastOutputFolder: ""
    }

    onAboutToShow: {
        backdropClosing = false
        if (!Analysis.exportBusy) exportStatus = ""
        if (!baseNameCustomized) prefixField.text = defaultBaseName()
        centerInWindow()
        Qt.callLater(restoreQualityChoice)
    }
    onAboutToHide: backdropClosing = true
    onSelectedRowsChanged: refreshClasses()
    onResultsChanged: refreshClasses()

    modal: true
    focus: true
    padding: 0
    width: Math.min(600, rootWindow ? rootWindow.width - 40 : 560)
    height: Math.min(700, rootWindow ? rootWindow.height - 32 : 660)
    x: 0
    y: 0
    transformOrigin: Item.Center
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionRevealDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Theme.motionRevealDuration; easing.type: Easing.OutQuint }
            NumberAnimation { target: root; property: "entranceOffset"; from: 28; to: 0; duration: Theme.motionRevealDuration; easing.type: Easing.OutQuint }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
        }
    }
    closePolicy: Analysis.exportBusy ? Popup.CloseOnEscape : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: GraphicsInfo.api === GraphicsInfo.Software ? "#99000000" : "transparent" }
    background: Rectangle {
        transform: Translate { y: root.entranceOffset }
        color: Theme.heroPanel
        radius: Theme.radiusOverlay
        border.color: Theme.hairline
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadowOverlay
            shadowOpacity: 0.78
            shadowBlur: 1.0
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 20
        }
    }

    FolderDialog {
        id: folderDialog
        title: L10n.t("export.chooseFolder")
        onAccepted: root.rememberFolder(selectedFolder)
    }
    Connections {
        target: Analysis
        function onExportBusyChanged() { if (Analysis.exportBusy) root.exportStatus = "" }
        function onExportFinished(success, message) {
            root.exportStatus = message
            if (success) root.close()
        }
    }

    contentItem: Flickable {
        transform: Translate { y: root.entranceOffset }
        clip: true
        contentWidth: width
        contentHeight: exportColumn.height + 48
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: exportColumn
            x: 24
            y: 24
            width: parent.width - 48
            spacing: 11

            Item {
                width: parent.width
                height: 54
                Column {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    width: parent.width - 42
                    spacing: 4
                    Text {
                        text: L10n.t("export.title")
                        color: Theme.textPrimary
                        font.family: Theme.displayFont
                        font.pixelSize: 25
                    }
                    Text {
                        text: root.selectedIndexes().length + " " + L10n.t("export.selected")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
                PfIconButton {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"
                    accessibleName: L10n.t("common.close")
                    onClicked: root.close()
                }
                MouseArea {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 42
                    property real pressX
                    property real pressY
                    property real startX
                    property real startY
                    cursorShape: Qt.SizeAllCursor
                    onPressed: {
                        pressX = mouse.x; pressY = mouse.y
                        startX = root.x; startY = root.y
                    }
                    onPositionChanged: if (pressed && rootWindow) {
                        root.userPositioned = true
                        root.x = Math.max(12, Math.min(rootWindow.width - root.width - 12, startX + mouse.x - pressX))
                        root.y = Math.max(12, Math.min(rootWindow.height - root.height - 12, startY + mouse.y - pressY))
                    }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }

            Text {
                text: L10n.t("export.format")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
            PfComboBox {
                id: exportFormat
                width: parent.width
                model: [L10n.t("export.videoFormat"), "JSON", "CSV", "TXT"]
                currentIndex: 0
                Accessible.name: L10n.t("export.format")
            }

            PfCheckBox {
                id: mergeCheck
                objectName: "mergeChronologicalCheck"
                width: parent.width
                visible: root.selectedFormat === "FFMPEG"
                enabled: !Analysis.exportBusy
                text: L10n.t("ui.325")
                onCheckedChanged: if (!root.baseNameCustomized) prefixField.text = root.defaultBaseName()
            }

            Column {
                width: parent.width
                spacing: 7
                visible: root.selectedFormat === "FFMPEG"
                enabled: !Analysis.exportBusy

                Text {
                    text: L10n.t("ui.326")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
                PfComboBox {
                    id: qualityMode
                    objectName: "exportQualityMode"
                    width: parent.width
                    currentIndex: root.qualityChoice
                    model: [L10n.translate("Lossless", "Без потери качества"), L10n.translate("High", "Высокое"), L10n.translate("Custom bitrate and resolution", "Свой битрейт и разрешение")]
                    Accessible.name: L10n.t("ui.326")
                    onModelChanged: Qt.callLater(root.restoreQualityChoice)
                    onActivated: function(index) {
                        if (index === 0 && signTransitionsCheck.checked) {
                            root.setHighQuality()
                            return
                        }
                        root.qualityChoice = index
                        if (index === 0) containerMode.currentIndex = 1
                    }
                }

                Row {
                    width: parent.width
                    spacing: 7
                    visible: root.customQuality
                    PfTextField {
                        id: bitrateField
                        objectName: "exportBitrateField"
                        width: (parent.width - 14) / 3
                        text: "30"
                        placeholderText: L10n.t("ui.327")
                        validator: DoubleValidator { bottom: 1; top: 500; decimals: 3; locale: "en_US" }
                        Accessible.name: L10n.t("ui.328")
                    }
                    PfTextField {
                        id: widthField
                        objectName: "exportMaxWidthField"
                        width: (parent.width - 14) / 3
                        placeholderText: L10n.t("ui.329")
                        validator: IntValidator { bottom: 2; top: 32768 }
                        Accessible.name: L10n.t("ui.330")
                    }
                    PfTextField {
                        id: heightField
                        objectName: "exportMaxHeightField"
                        width: (parent.width - 14) / 3
                        placeholderText: L10n.t("ui.331")
                        validator: IntValidator { bottom: 2; top: 32768 }
                        Accessible.name: L10n.t("ui.332")
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    text: root.losslessExport
                        ? (L10n.t("ui.333"))
                        : signTransitionsCheck.checked
                            ? (L10n.t("ui.334"))
                            : (L10n.t("ui.335"))
                }
            }

            PfCheckBox {
                id: signTransitionsCheck
                objectName: "signEachTransitionCheck"
                width: parent.width
                visible: root.selectedFormat === "FFMPEG"
                enabled: !Analysis.exportBusy
                text: L10n.t("ui.336")
                tooltipText: L10n.t("ui.337")
                onCheckedChanged: if (checked && root.losslessExport) root.setHighQuality()
            }

            Row {
                width: parent.width
                spacing: 8
                PfTextField {
                    id: folderField
                    width: parent.width - 96
                    text: root.outputFolder
                    placeholderText: L10n.t("export.folder")
                    Accessible.name: L10n.t("export.folder")
                    onEditingFinished: root.rememberFolder(text)
                }
                PfButton {
                    width: 88
                    text: L10n.t("export.chooseFolder")
                    quiet: true
                    enabled: !Analysis.exportBusy
                    onClicked: root.chooseFolder()
                }
            }

            Row {
                width: parent.width
                spacing: 7
                PfTextField {
                    id: prefixField
                    objectName: "exportBaseNameField"
                    width: root.selectedFormat === "FFMPEG" ? parent.width - containerMode.width - 22 : parent.width
                    text: root.defaultBaseName()
                    placeholderText: L10n.t("ui.338")
                    Accessible.name: L10n.t("ui.339")
                    onTextEdited: root.baseNameCustomized = true
                }
                Text {
                    visible: root.selectedFormat === "FFMPEG"
                    width: 8
                    height: prefixField.height
                    text: "|"
                    color: Theme.textDisabled
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }
                PfComboBox {
                    id: containerMode
                    objectName: "exportContainerMode"
                    visible: root.selectedFormat === "FFMPEG"
                    width: 84
                    model: [".mp4", ".mov", ".mkv", ".webm"]
                    currentIndex: 0
                    Accessible.name: L10n.t("ui.340")
                    enabled: !Analysis.exportBusy && !root.losslessExport
                    onActivated: function(index) {
                        if (root.losslessExport && index !== 1) root.setHighQuality()
                    }
                }
            }

            Text {
                objectName: "exportStatusText"
                width: parent.width
                text: root.exportStatus ? L10n.status(root.exportStatus) : (root.selectedFormat === "FFMPEG"
                    ? (root.mergeChronological
                        ? (L10n.t("ui.341"))
                        : (L10n.t("ui.342")))
                    : L10n.t("export.hint"))
                color: root.exportStatus ? Theme.accent : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            PfButton {
                objectName: "startExportButton"
                width: parent.width
                primary: true
                text: Analysis.exportBusy
                    ? root.runningLabel + " " + Analysis.exportCompleted + "/" + Analysis.exportTotal
                        + (Analysis.exportStage === "preparing" ? "" : " · " + Analysis.exportClipProgress + "%")
                    : L10n.t("export.prepare")
                enabled: !Analysis.exportBusy && root.qualityValid
                    && root.fileBaseName.length > 0 && root.selectedIndexes().length > 0
                    && root.outputFolder.length > 0
                onClicked: root.startExport()
            }
            PfButton {
                width: parent.width
                visible: Analysis.exportBusy
                text: L10n.t("export.cancel")
                onClicked: Analysis.cancelExport()
            }
        }
    }

    function centerInWindow() {
        if (!rootWindow || root.userPositioned) return
        root.x = Math.round((rootWindow.width - root.width) / 2)
        root.y = Math.round((rootWindow.height - root.height) / 2)
    }
    onOpened: { refreshClasses(); centerInWindow() }
    Connections {
        target: root.rootWindow
        function onWidthChanged() { if (root.visible) Qt.callLater(root.centerInWindow) }
        function onHeightChanged() { if (root.visible) Qt.callLater(root.centerInWindow) }
    }
}
