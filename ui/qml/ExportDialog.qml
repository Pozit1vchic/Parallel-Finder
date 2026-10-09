import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import PfUi
import PfUiBridge

Popup {
    id: root
    objectName: "exportDialog"
    property bool backdropClosing: false
    onAboutToShow: { backdropClosing = false; centerInWindow(); Qt.callLater(restoreQualityChoice) }
    onAboutToHide: backdropClosing = true
    property int qualityChoice: 1
    function restoreQualityChoice() { qualityMode.currentIndex = qualityChoice }
    readonly property int encodingQuality: [14, 18, 22, 18][qualityChoice]
    readonly property int videoBitrateKbps: qualityChoice === 3 ? Math.round(Number(bitrateField.text.replace(",", ".")) * 1000) : 0
    readonly property bool qualityValid: qualityChoice !== 3 || (isFinite(videoBitrateKbps) && videoBitrateKbps >= 1000 && videoBitrateKbps <= 500000)
    readonly property int selectedNumbering: exportNumbering.currentIndex
    readonly property int selectedCutMode: mergeChronological ? 0 : cutMode.currentIndex
    readonly property bool mergeChronological: selectedFormat === "FFMPEG" && mergeCheck.checked
    readonly property string selectedFormat: ["FFMPEG", "JSON", "CSV", "TXT"][exportFormat.currentIndex] || "FFMPEG"
    readonly property string fileBaseName: prefixField.text
    property real entranceOffset: 0
    property var rootWindow
    property var selectedRows: ({})
    property var selectionOrder: []
    property string outputFolder: ""
    property string exportStatus: ""
    property var results: { Analysis.resultCategoryRevision; return Analysis.results }
    property var colorOrder: []
    readonly property var palette: ["", "#d56565", "#638edb", "#7c9885", "#aa83d4", "#d5ad63"]
    readonly property var colorLabels: ["colors.none", "colors.orange", "colors.blue", "colors.green", "colors.purple", "colors.gold"]
    function refreshColors() {
        const present = []
        for (const item of root.results) {
            if (root.selectedRows[item.id] !== true) continue
            const color = String(item.categoryColor || "").toLowerCase()
            if (present.indexOf(color) < 0) present.push(color)
        }
        const next = colorOrder.filter(function(color) { return present.indexOf(color) >= 0 })
        for (const color of present) if (next.indexOf(color) < 0) next.push(color)
        colorOrder = next
    }
    function moveColor(index, direction) {
        const target = index + direction
        if (index < 0 || target < 0 || target >= colorOrder.length) return
        const next = colorOrder.slice()
        const color = next.splice(index, 1)[0]
        next.splice(target, 0, color)
        colorOrder = next
    }
    onSelectedRowsChanged: refreshColors()
    onResultsChanged: refreshColors()

    property bool userPositioned: false
    modal: true; focus: true; padding: 0
    width: Math.min(600, rootWindow ? rootWindow.width - 40 : 560)
    height: Math.min(700, rootWindow ? rootWindow.height - 32 : 660)
    x: 0; y: 0
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
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: GraphicsInfo.api === GraphicsInfo.Software ? "#99000000" : "transparent" }
    background: Rectangle {
        transform: Translate { y: root.entranceOffset } color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairline; layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowOverlay; shadowOpacity: 0.78; shadowBlur: 1.0; shadowHorizontalOffset: 0; shadowVerticalOffset: 20 } }
    FolderDialog {
        id: folderDialog
        title: L10n.t("export.chooseFolder")
        onAccepted: {
            root.outputFolder = root.localFolderPath(selectedFolder)
        }
    }
    Connections { target: Analysis; function onExportFinished(success, message) { root.exportStatus = message; if (success) root.close() } }
    function selectedIndexes() {
        const selected = Object.keys(root.selectedRows).filter(function(key) { return root.selectedRows[key] === true }).map(Number)
        const ordered = selectionOrder.filter(function(id) { return selected.indexOf(Number(id)) >= 0 }).map(Number)
        for (const id of selected) if (ordered.indexOf(id) < 0) ordered.push(id)
        return ordered
    }
    function localFolderPath(value) {
        if (value && value.toLocalFile) {
            const local = value.toLocalFile()
            if (local && local.length > 0) return local
        }
        let text = String(value || "")
        if (text.toLowerCase().startsWith("file:")) {
            try { text = decodeURIComponent(text.replace(/^file:\/\//i, "")) } catch (error) { text = text.replace(/^file:\/\//i, "") }
        }
        if (/^\/[A-Za-z]:/.test(text)) text = text.slice(1)
        return text
    }
    function chooseFolder() {
        if (root.outputFolder.length > 0)
            folderDialog.currentFolder = "file:///" + root.outputFolder.replace(/\\/g, "/")
        folderDialog.open()
    }
    contentItem: Flickable {
        transform: Translate { y: root.entranceOffset }
        clip: true
        contentWidth: width
        contentHeight: exportColumn.height + 48
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column { id: exportColumn; x: 24; y: 24; width: parent.width - 48; spacing: 14
        Item { width: parent.width; height: 56
            Column { anchors.left: parent.left; anchors.top: parent.top; width: parent.width - 42; spacing: 4
                Text { text: L10n.t("export.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 25 }
                Text { font.family: Theme.fontFamily; text: Object.keys(root.selectedRows).length + " " + L10n.t("export.selected"); color: Theme.textSecondary; font.pixelSize: 12 }
            }
            PfIconButton { anchors.right: parent.right; anchors.top: parent.top; iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: root.close() }
            MouseArea {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.rightMargin: 42
                property real pressX
                property real pressY
                property real startX
                property real startY
                cursorShape: Qt.SizeAllCursor
                onPressed: { pressX = mouse.x; pressY = mouse.y; startX = root.x; startY = root.y }
                onPositionChanged: if (pressed && rootWindow) {
                    root.userPositioned = true
                    root.x = Math.max(12, Math.min(rootWindow.width - root.width - 12, startX + mouse.x - pressX))
                    root.y = Math.max(12, Math.min(rootWindow.height - root.height - 12, startY + mouse.y - pressY))
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
        Text { font.family: Theme.fontFamily; text: L10n.t("export.format"); color: Theme.textSecondary; font.pixelSize: 11 }
        PfComboBox { id: exportFormat; width: parent.width; model: [L10n.t("export.videoFormat"), "JSON", "CSV", "TXT"]; currentIndex: 0; Accessible.name: L10n.t("export.format") }
        PfCheckBox {
            id: mergeCheck
            objectName: "mergeChronologicalCheck"
            width: parent.width
            visible: root.selectedFormat === "FFMPEG"
            enabled: !Analysis.exportBusy
            text: L10n.t("export.mergeChronological")
        }
        Column {
            objectName: "exportColorOrderPanel"
            width: parent.width; spacing: 6; visible: root.colorOrder.length > 0
            enabled: !Analysis.exportBusy
            Text { text: L10n.t("export.colorOrder"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
            Repeater {
                model: root.colorOrder
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: parent.width; height: 36; radius: Theme.radiusButton
                    color: Theme.well; border.color: Theme.hairline
                    Row {
                        anchors.fill: parent; anchors.margins: 4; spacing: 8
                        Rectangle { width: 18; height: 18; y: 5; radius: 4; color: modelData || Theme.surfaceMuted; border.color: Theme.hairlineStrong }
                        Text { width: parent.width - 106; height: 28; verticalAlignment: Text.AlignVCenter; text: root.palette.indexOf(modelData) >= 0 ? L10n.t(root.colorLabels[root.palette.indexOf(modelData)]) : modelData; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                        PfButton { objectName: "exportColorUp" + index; width: 28; height: 28; text: "↑"; Accessible.name: L10n.t("export.moveUp"); enabled: index > 0; onClicked: root.moveColor(index, -1) }
                        PfButton { objectName: "exportColorDown" + index; width: 28; height: 28; text: "↓"; Accessible.name: L10n.t("export.moveDown"); enabled: index < root.colorOrder.length - 1; onClicked: root.moveColor(index, 1) }
                    }
                }
            }
            Text { width: parent.width; text: L10n.t("export.colorOrderHint"); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
        }
        Text { font.family: Theme.fontFamily; text: L10n.t("export.numbering"); color: Theme.textSecondary; font.pixelSize: 11 }
            Row { width: parent.width; spacing: 8; enabled: !root.mergeChronological
                PfButton { width: (parent.width - 8) / 2; objectName: "videoNumberingButton"; text: L10n.t("export.asVideo"); selected: exportNumbering.currentIndex === 0; onClicked: exportNumbering.currentIndex = 0 }
                PfButton { width: (parent.width - 8) / 2; objectName: "sortNumberingButton"; text: L10n.t("export.bySort"); selected: exportNumbering.currentIndex === 1; onClicked: exportNumbering.currentIndex = 1 }
            }
        ComboBox { id: exportNumbering; visible: false; model: [0, 1]; currentIndex: 0 }
        Text { font.family: Theme.fontFamily; text: L10n.t("export.cutMode"); color: Theme.textSecondary; font.pixelSize: 11 }
        Row { width: parent.width; spacing: 8; enabled: !root.mergeChronological
            PfButton { width: (parent.width - 8) / 2; objectName: "exactCutButton"; text: L10n.t("export.exact"); selected: cutMode.currentIndex === 0; onClicked: cutMode.currentIndex = 0 }
            PfButton { width: (parent.width - 8) / 2; objectName: "fastCutButton"; text: L10n.t("export.fast"); selected: cutMode.currentIndex === 1; onClicked: cutMode.currentIndex = 1 }
        }
        ComboBox { id: cutMode; visible: false; model: [0, 1]; currentIndex: 0 }
        Column {
            width: parent.width; spacing: 8; visible: root.selectedFormat === "FFMPEG"
            enabled: !Analysis.exportBusy
            Text { text: L10n.language === "ru" ? "Качество видео" : "Video quality"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
            PfComboBox {
                id: qualityMode; objectName: "exportQualityMode"; width: parent.width; currentIndex: 1
                Component.onCompleted: Qt.callLater(root.restoreQualityChoice)
                onModelChanged: Qt.callLater(root.restoreQualityChoice)
                onActivated: function(index) { root.qualityChoice = index }
                enabled: root.selectedCutMode === 0
                model: L10n.language === "ru" ? ["Максимальное", "Высокое · рекомендуется", "Баланс качества и размера", "Свой битрейт"] : ["Maximum", "High · recommended", "Balance quality and size", "Custom bitrate"]
            }
            Row {
                width: parent.width; spacing: 8; visible: root.selectedCutMode === 0 && qualityChoice === 3
                PfTextField { id: bitrateField; objectName: "exportBitrateField"; width: parent.width - 60; text: "30"; validator: DoubleValidator { bottom: 1; top: 500; decimals: 3; locale: "en_US" } Accessible.name: L10n.language === "ru" ? "Битрейт видео" : "Video bitrate" }
                Text { width: 52; height: bitrateField.height; verticalAlignment: Text.AlignVCenter; text: L10n.language === "ru" ? "Мбит/с" : "Mbps"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11
                text: root.selectedCutMode === 1
                    ? (L10n.language === "ru" ? "Быстрый режим копирует исходное видео без перекодирования и потери качества; границы зависят от ключевых кадров." : "Fast mode copies the original video without re-encoding or quality loss; boundaries depend on keyframes.")
                    : (L10n.language === "ru" ? "Разрешение сохраняется. Высокое качество — по умолчанию; максимальное увеличивает размер файла. Битрейт — целевой, итоговый зависит от сцены. При перекодировании возможны потери." : "Source resolution is preserved. High quality is the default; maximum increases file size. Bitrate is a target and varies with the scene. Re-encoding can introduce loss.")
            }
        }
        Row { width: parent.width; spacing: 8
            PfTextField { id: folderField; width: parent.width - 110; text: root.outputFolder; placeholderText: L10n.t("export.folder"); Accessible.name: L10n.t("export.folder"); onEditingFinished: root.outputFolder = text }
            PfButton { width: 102; text: L10n.t("export.chooseFolder"); quiet: true; onClicked: root.chooseFolder() }
        }
        PfTextField { id: prefixField; width: parent.width; text: "frame"; placeholderText: L10n.t("export.prefix"); Accessible.name: L10n.t("export.prefix") }
        Text { font.family: Theme.fontFamily;
            width: parent.width
            text: root.exportStatus || (root.selectedFormat === "FFMPEG"
                ? L10n.t(root.mergeChronological ? "export.mergeHint" : "export.ffmpegHint")
                : L10n.t("export.hint"))
            color: root.exportStatus ? Theme.accent : Theme.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        PfButton { width: parent.width; primary: true; text: Analysis.exportBusy ? L10n.t("export.running") + " " + Analysis.exportCompleted + "/" + Analysis.exportTotal + " · " + Analysis.exportClipProgress + "%" : L10n.t("export.prepare"); enabled: !Analysis.exportBusy && (root.selectedFormat !== "FFMPEG" || root.selectedCutMode === 1 || root.qualityValid) && Object.keys(root.selectedRows).length > 0 && root.outputFolder.length > 0; onClicked: Analysis.exportResults(root.selectedFormat, exportNumbering.currentIndex, root.selectedCutMode, root.outputFolder, prefixField.text, root.selectedIndexes(), root.mergeChronological, root.colorOrder, root.encodingQuality, root.selectedFormat === "FFMPEG" && root.selectedCutMode === 0 ? root.videoBitrateKbps : 0) }
        PfButton { width: parent.width; visible: Analysis.exportBusy; text: L10n.t("export.cancel"); onClicked: Analysis.cancelExport() }
        }
    }

    function centerInWindow() {
        if (!rootWindow || root.userPositioned) return
        root.x = Math.round((rootWindow.width - root.width) / 2)
        root.y = Math.round((rootWindow.height - root.height) / 2)
    }
    onOpened: { refreshColors(); centerInWindow() }
}
