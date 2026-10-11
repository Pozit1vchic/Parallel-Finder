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
    onAboutToShow: { backdropClosing = false; if (!Analysis.exportBusy) exportStatus = ""; centerInWindow(); Qt.callLater(restoreQualityChoice) }
    onAboutToHide: backdropClosing = true
    property int qualityChoice: 1
    function restoreQualityChoice() { qualityMode.currentIndex = qualityChoice }
    readonly property int encodingQuality: [14, 18, 22, 18, 18][qualityChoice]
    readonly property bool losslessExport: qualityChoice === 4
    readonly property int videoBitrateKbps: qualityChoice === 3 ? Math.round(Number(bitrateField.text.replace(",", ".")) * 1000) : 0
    readonly property bool qualityValid: qualityChoice !== 3 || (isFinite(videoBitrateKbps) && videoBitrateKbps >= 1000 && videoBitrateKbps <= 500000)
    readonly property int selectedNumbering: exportNumbering.currentIndex
    readonly property int selectedCutMode: losslessExport ? 1 : mergeChronological ? 0 : cutMode.currentIndex
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
    property var classOrder: []
    property bool classOrderCustomized: false
    function refreshClasses() {
        const present = []
        for (const item of root.results) {
            if (root.selectedRows[item.id] !== true || item.hidden) continue
            const classification = L10n.movementKey(item.classification)
            if (present.indexOf(classification) < 0) present.push(classification)
        }
        const next = classOrderCustomized
            ? classOrder.filter(function(type) { return present.indexOf(type) >= 0 }) : []
        for (const type of L10n.movementTypes) if (present.indexOf(type) >= 0 && next.indexOf(type) < 0) next.push(type)
        classOrder = next
    }
    function moveClass(index, direction) {
        const target = index + direction
        if (index < 0 || target < 0 || target >= classOrder.length) return
        const next = classOrder.slice()
        const color = next.splice(index, 1)[0]
        next.splice(target, 0, color)
        classOrderCustomized = true
        classOrder = next
    }
    onSelectedRowsChanged: refreshClasses()
    onResultsChanged: { classOrderCustomized = false; refreshClasses() }

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
    Connections {
        target: Analysis
        function onExportBusyChanged() { if (Analysis.exportBusy) root.exportStatus = "" }
        function onExportFinished(success, message) { root.exportStatus = message; if (success) root.close() }
    }
    function startExport() {
        root.exportStatus = ""
        Analysis.exportResults(root.selectedFormat, exportNumbering.currentIndex, root.selectedCutMode, root.outputFolder, prefixField.text, root.selectedIndexes(), root.mergeChronological, root.classOrder, root.encodingQuality, root.selectedFormat === "FFMPEG" && root.selectedCutMode === 0 ? root.videoBitrateKbps : 0, classFoldersCheck.checked)
    }
    readonly property string runningLabel: Analysis.exportStage === "preparing"
        ? (L10n.language === "ru" ? "Подготовка клипа…" : "Preparing clip…")
        : Analysis.exportStage === "finalizing"
            ? (L10n.language === "ru" ? "Сборка ролика…" : "Assembling video…")
            : L10n.t("export.running")
    function selectedIndexes() {
        const selected = root.results.filter(function(row) { return root.selectedRows[row.id] === true && !row.hidden }).map(function(row) { return Number(row.id) })
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
                Text { font.family: Theme.fontFamily; text: root.selectedIndexes().length + " " + L10n.t("export.selected"); color: Theme.textSecondary; font.pixelSize: 12 }
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
            text: L10n.language === "ru" ? "Один общий ролик по порядку классов" : "One combined video in movement-type order"
        }
        Column {
            objectName: "exportClassOrderPanel"
            width: parent.width; spacing: 6; visible: root.classOrder.length > 0
            enabled: !Analysis.exportBusy
            Text { text: L10n.language === "ru" ? "Порядок типов движений" : "Movement-type order"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
            Repeater {
                model: root.classOrder
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: parent.width; height: 36; radius: Theme.radiusButton
                    color: Theme.well; border.color: Theme.hairline
                    Row {
                        anchors.fill: parent; anchors.margins: 4; spacing: 8
                        Text { width: parent.width - 78; height: 28; verticalAlignment: Text.AlignVCenter; text: L10n.classificationLabel(modelData); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                        PfButton { objectName: "exportClassUp" + index; width: 28; height: 28; text: "↑"; Accessible.name: L10n.t("export.moveUp"); enabled: index > 0; onClicked: root.moveClass(index, -1) }
                        PfButton { objectName: "exportClassDown" + index; width: 28; height: 28; text: "↓"; Accessible.name: L10n.t("export.moveDown"); enabled: index < root.classOrder.length - 1; onClicked: root.moveClass(index, 1) }
                    }
                }
            }
            Text { width: parent.width; text: L10n.language === "ru" ? "Типы идут сверху вниз для клипов и общего ролика. Нумерация сквозная; внутри типа клипы следуют выбранному порядку." : "Types run top to bottom for separate clips and the combined video. Clip numbers are continuous; clips within each type follow the selected order."; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
        }
        PfCheckBox {
            id: classFoldersCheck; objectName: "exportClassFoldersCheck"; checked: true
            visible: root.selectedFormat === "FFMPEG" && !root.mergeChronological
            enabled: !Analysis.exportBusy
            text: L10n.language === "ru" ? "Папки по типам движений и тип в имени клипа" : "Movement-type folders and type in clip names"
        }
        Text { font.family: Theme.fontFamily; text: L10n.t("export.numbering"); color: Theme.textSecondary; font.pixelSize: 11 }
            Row { width: parent.width; spacing: 8; enabled: !root.mergeChronological
                PfButton { width: (parent.width - 8) / 2; objectName: "videoNumberingButton"; text: L10n.t("export.asVideo"); selected: exportNumbering.currentIndex === 0; onClicked: exportNumbering.currentIndex = 0 }
                PfButton { width: (parent.width - 8) / 2; objectName: "sortNumberingButton"; text: L10n.t("export.bySort"); selected: exportNumbering.currentIndex === 1; onClicked: exportNumbering.currentIndex = 1 }
            }
        ComboBox { id: exportNumbering; visible: false; model: [0, 1]; currentIndex: 0 }
        Text { font.family: Theme.fontFamily; text: L10n.t("export.cutMode"); color: Theme.textSecondary; font.pixelSize: 11 }
        Row { width: parent.width; spacing: 8; enabled: !root.mergeChronological && !root.losslessExport && !Analysis.exportBusy
            PfButton { width: (parent.width - 8) / 2; objectName: "exactCutButton"; text: L10n.t("export.exact"); selected: root.selectedCutMode === 0; onClicked: cutMode.currentIndex = 0 }
            PfButton { width: (parent.width - 8) / 2; objectName: "fastCutButton"; text: L10n.t("export.fast"); selected: root.selectedCutMode === 1; onClicked: cutMode.currentIndex = 1 }
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
                enabled: root.selectedCutMode === 0 || root.losslessExport
                model: L10n.language === "ru" ? ["Максимальное", "Высокое · рекомендуется", "Баланс качества и размера", "Свой битрейт", "Без потери качества · без перекодирования"] : ["Maximum", "High · recommended", "Balance quality and size", "Custom bitrate", "No quality loss · no re-encoding"]
            }
            Row {
                width: parent.width; spacing: 8; visible: root.selectedCutMode === 0 && qualityChoice === 3
                PfTextField { id: bitrateField; objectName: "exportBitrateField"; width: parent.width - 60; text: "30"; validator: DoubleValidator { bottom: 1; top: 500; decimals: 3; locale: "en_US" } Accessible.name: L10n.language === "ru" ? "Битрейт видео" : "Video bitrate" }
                Text { width: 52; height: bitrateField.height; verticalAlignment: Text.AlignVCenter; text: L10n.language === "ru" ? "Мбит/с" : "Mbps"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11
                text: root.selectedCutMode === 1
                    ? (L10n.language === "ru" ? "Качество исходника сохраняется без перекодирования. Начало может захватить контекст от предыдущего ключевого кадра. Для обрезки строго по параллели выбери перекодирование и «Точно». Параметры исходников общего ролика должны совпадать." : "Source quality is preserved without re-encoding. The start may include context from the preceding keyframe. For strict parallel boundaries, choose re-encoding and Exact. Combined-video sources must have compatible streams.")
                    : (L10n.language === "ru" ? "Режим «Точно» обрезает по границам найденной параллели без добавленных секунд. Разрешение сохраняется; максимальное качество увеличивает размер файла. При перекодировании возможны потери." : "Exact mode cuts at the matched parallel boundaries without added seconds. Source resolution is preserved; maximum quality increases file size. Re-encoding can introduce loss.")
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
                ? (root.mergeChronological && root.selectedCutMode === 1
                    ? (L10n.language === "ru" ? "Один MP4 по порядку типов движений без перекодирования видео и звука. Точные дубликаты исключаются. Несовместимые исходники не перекодируются автоматически." : "One MP4 in movement-type order without re-encoding video or audio. Exact duplicates are excluded. Incompatible sources are never automatically re-encoded.")
                    : L10n.t(root.mergeChronological ? "export.mergeHint" : "export.ffmpegHint"))
                : L10n.t("export.hint"))
            color: root.exportStatus ? Theme.accent : Theme.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        PfButton { objectName: "startExportButton"; width: parent.width; primary: true; text: Analysis.exportBusy ? root.runningLabel + " " + Analysis.exportCompleted + "/" + Analysis.exportTotal + (Analysis.exportStage === "preparing" ? "" : " · " + Analysis.exportClipProgress + "%") : L10n.t("export.prepare"); enabled: !Analysis.exportBusy && (root.selectedFormat !== "FFMPEG" || root.selectedCutMode === 1 || root.qualityValid) && root.selectedIndexes().length > 0 && root.outputFolder.length > 0; onClicked: root.startExport() }
        PfButton { width: parent.width; visible: Analysis.exportBusy; text: L10n.t("export.cancel"); onClicked: Analysis.cancelExport() }
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
