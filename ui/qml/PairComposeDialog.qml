import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi
import PfUiBridge

ReviewDialog {
    id: root
    objectName: "pairComposeDialog"
    animateScale: false
    title: L10n.t("ui.346")
    property var record: null
    property var candidates: []
    property string errorText: ""
    property var sortedRows: []
    property string searchText: ""
    property string sourceFilter: ""
    readonly property var sourceChoices: {
        const paths = new Set()
        for (const row of sortedRows) {
            if (!record || row.id === record.id || row.hidden) continue
            if (!row.leftRemoved && row.leftSource) paths.add(row.leftSource)
            if (!row.rightRemoved && row.rightSource) paths.add(row.rightSource)
        }
        return ["", ...Array.from(paths).sort((a, b) => fileName(a).localeCompare(fileName(b)))]
    }
    property int preferredDonorSide: 0
    property int preferredTargetSide: 0
    property int selectedDonorId: -1
    readonly property var donor: candidates[donorChoice.currentIndex] || null
    readonly property bool targetSingle: !!record && Boolean(record.leftRemoved) !== Boolean(record.rightRemoved)
    readonly property bool donorSingle: !!donor && Boolean(donor.leftRemoved) !== Boolean(donor.rightRemoved)
    readonly property bool bothSingles: targetSingle && donorSingle
    readonly property int targetSideIndex: bothSingles ? (record.leftRemoved ? 0 : 1) : preferredTargetSide
    readonly property int donorSideIndex: donorSingle ? (donor.leftRemoved ? 1 : 0) : preferredDonorSide
    readonly property string targetSide: targetSideIndex === 0 ? "left" : "right"
    readonly property string donorSide: donorSideIndex === 0 ? "left" : "right"
    readonly property string donorPreview: donor ? donor[donorSide + "Preview"] || "" : ""
    readonly property bool sameMoment: {
        if (!donor || !record) return false
        const partner = targetSide === "left" ? "right" : "left"
        return !record[partner + "Removed"]
            && donor[donorSide + "Source"] === record[partner + "Source"]
            && Math.abs(donor[donorSide + "ClipStart"] - record[partner + "ClipStart"]) < 0.000001
            && Math.abs(donor[donorSide + "ClipEnd"] - record[partner + "ClipEnd"]) < 0.000001
    }

    function fileName(path) {
        const parts = String(path || "").replace(/\\/g, "/").split("/")
        return parts[parts.length - 1] || (L10n.translate("unknown source", "источник неизвестен"))
    }
    function timecode(seconds) {
        const total = Math.max(0, Number(seconds) || 0)
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const secs = Math.floor(total % 60)
        return [hours, minutes, secs].map(function(value) { return String(value).padStart(2, "0") }).join(":")
    }
    function pairChoiceLabel(row) {
        const left = row.leftRemoved ? (L10n.translate("A removed", "A удалена")) : "A " + fileName(row.leftSource) + " · " + timecode(row.leftStart)
        const right = row.rightRemoved ? (L10n.translate("B removed", "B удалена")) : "B " + fileName(row.rightSource) + " · " + timecode(row.rightStart)
        return "№ " + (Number(row.id) + 1) + " · " + L10n.classificationLabel(row.classification) + " · " + left + " ↔ " + right
    }
    function sideChoiceLabel(row, side, removed) {
        const letter = side === "left" ? "A" : "B"
        const name = side === "left" ? row.leftSource : row.rightSource
        const start = side === "left" ? row.leftStart : row.rightStart
        return letter + (removed ? (L10n.translate(" · removed", " · удалено")) : " · " + fileName(name) + " · " + timecode(start))
    }

    function refreshCandidates() {
        if (!record) return
        const query = searchText.trim().toLowerCase()
        candidates = sortedRows.filter(function(row) {
            return row.id !== record.id && !row.hidden
                && (!sourceFilter || (!row.leftRemoved && row.leftSource === sourceFilter)
                    || (!row.rightRemoved && row.rightSource === sourceFilter))
                && (!query || root.pairChoiceLabel(row).toLowerCase().includes(query))
        })
        const previous = candidates.findIndex(function(row) { return row.id === root.selectedDonorId })
        donorChoice.currentIndex = previous >= 0 ? previous : (candidates.length ? 0 : -1)
    }
    function warmNearby() {
        if (!visible || donorChoice.currentIndex < 0) return
        const urls = []
        for (const row of Review.nearbyRows(candidates, donorChoice.currentIndex)) {
            const side = row[donorSide + "Removed"] ? (donorSide === "left" ? "right" : "left") : donorSide
            if (row[side + "Preview"]) urls.push(row[side + "Preview"])
        }
        Analysis.prefetchPreviews(urls)
    }
    onSortedRowsChanged: if (visible) refreshCandidates()
    onSearchTextChanged: if (visible) refreshCandidates()
    onSourceFilterChanged: if (visible) refreshCandidates()
    onSourceChoicesChanged: if (sourceFilter && !sourceChoices.includes(sourceFilter)) sourceFilter = ""
    onDonorChanged: { errorText = ""; prefetchTimer.restart() }
    onDonorSideChanged: { errorText = ""; prefetchTimer.restart() }
    onTargetSideChanged: errorText = ""
    onAboutToShow: {
        errorText = ""
        searchText = ""
        refreshCandidates()
        prefetchTimer.restart()
    }
    onOpened: donorSearch.forceActiveFocus()
    onAboutToHide: prefetchTimer.stop()
    Timer { id: prefetchTimer; interval: 35; onTriggered: root.warmNearby() }
    Connections {
        target: Analysis
        function onResultsChanged() {
            root.selectedDonorId = -1
            if (root.visible) root.close()
        }
        function onResultCategoriesChanged() {
            if (!root.visible || !root.record) return
            const current = Analysis.results.find(row => row.id === root.record.id)
            if (!current || current.hidden) root.close()
            else root.record = current
        }
    }

    Item {
        id: stage
        anchors.fill: parent
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: root.bothSingles ? (L10n.translate("The remaining moments will be combined; the original pairs can be restored.", "Оставшиеся моменты объединятся; исходные пары можно восстановить.")) : (L10n.translate("Choose a side and a moment from another pair. The original pair stays intact.", "Выбери сторону и момент из другой пары. Исходная пара сохранится."))
                color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.preferredWidth: 110
                    Text { text: L10n.translate("Replace side", "Заменить сторону"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                    PfComboBox { id: targetChoice; objectName: "composeTargetChoice"; enabled: !root.bothSingles; Layout.fillWidth: true; selectionIndex: root.targetSideIndex; onActivated: index => root.preferredTargetSide = index; model: [L10n.translate("Side A", "Сторона A"), L10n.translate("Side B", "Сторона B")] }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Text { text: L10n.translate("Donor pair", "Пара для замены"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                    PfComboBox {
                        id: donorChoice; objectName: "composeDonorChoice"; Layout.fillWidth: true
                        model: root.candidates.map(function(row) { return root.pairChoiceLabel(row) })
                        onActivated: index => root.selectedDonorId = root.candidates[index].id
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                PfComboBox {
                    objectName: "composeSourceFilter"
                    Layout.preferredWidth: Math.min(265, root.width * .34)
                    Accessible.name: L10n.translate("Filter by source video", "Фильтр по исходному видео")
                    model: root.sourceChoices.map(path => path ? root.fileName(path)
                        : L10n.translate("All source videos", "Все исходные видео"))
                    selectionIndex: Math.max(0, root.sourceChoices.indexOf(root.sourceFilter))
                    onActivated: index => root.sourceFilter = root.sourceChoices[index] || ""
                }
                PfTextField {
                    id: donorSearch
                    objectName: "composeDonorSearch"
                    Layout.fillWidth: true
                    text: root.searchText
                    placeholderText: L10n.translate("Pair number, movement or video", "Номер пары, движение или видео")
                    onTextEdited: root.searchText = text
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                radius: 8; color: Theme.well; border.color: Theme.hairline
                RowLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 12
                    Image {
                        id: donorImage
                        objectName: "composeDonorPreview"
                        Layout.minimumWidth: 116; Layout.preferredWidth: 116; Layout.maximumWidth: 116; Layout.fillHeight: true
                        source: root.donorPreview; fillMode: Image.PreserveAspectFit
                        asynchronous: true; sourceSize.width: 960
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: L10n.translate("Moment", "Момент"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        PfComboBox {
                            id: donorSideChoice; objectName: "composeDonorSideChoice"; Layout.fillWidth: true; enabled: !root.donorSingle
                            selectionIndex: root.donorSideIndex
                            onActivated: index => root.preferredDonorSide = index
                            model: root.donor ? [root.sideChoiceLabel(root.donor, "left", root.donor.leftRemoved), root.sideChoiceLabel(root.donor, "right", root.donor.rightRemoved)] : ["A", "B"]
                        }
                    }
                }
            }
            Text { text: L10n.translate("Pair after replacement", "Пара после замены"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 13 }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 72; spacing: 12
                Repeater {
                    model: ["left", "right"]
                    delegate: Rectangle {
                        id: card
                        required property string modelData
                        readonly property bool replaced: modelData === root.targetSide && !!root.donor
                        readonly property bool absent: replaced ? false : (!!root.record && !!root.record[modelData + "Removed"])
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1
                        color: Theme.canvas; radius: 8; clip: true
                        border.color: replaced ? Theme.accent : Theme.hairline
                        border.width: replaced ? 2 : 1
                        Image {
                            id: preview
                            anchors.fill: parent; anchors.margins: 8; anchors.topMargin: 30; anchors.bottomMargin: 28
                            fillMode: Image.PreserveAspectFit
                            source: card.absent ? "" : card.replaced ? root.donorPreview : root.record ? root.record[card.modelData + "Preview"] || "" : ""
                            asynchronous: true; sourceSize.width: 960
                        }
                        Text { anchors.centerIn: parent; visible: card.absent; text: L10n.translate("Side removed", "Сторона удалена"); color: Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 12 }
                        Text { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 10; text: modelData === "left" ? "A" : "B"; color: card.replaced ? Theme.accent : Theme.textPrimary; font.family: Theme.fontFamily; font.bold: true }
                        Text {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10
                            text: card.absent ? (L10n.translate("Choose another moment", "Выбери другой момент")) : card.replaced ? (L10n.translate("From pair #", "Из пары № ")) + (Number(root.donor.id) + 1) + " · " + (root.donorSide === "left" ? "A" : "B") : (L10n.translate("Unchanged", "Без изменений"))
                            color: card.replaced ? Theme.accent : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; elide: Text.ElideRight
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !!root.errorText || root.sameMoment || !root.candidates.length
                text: root.errorText || (root.sameMoment
                    ? L10n.translate("Choose a different moment. This one is already on the other side.", "Выбери другой момент. Этот уже стоит на другой стороне.")
                    : L10n.translate("No matching source pairs.", "Подходящих пар не найдено."))
                color: Theme.accentText; wrapMode: Text.WordWrap; font.family: Theme.fontFamily; font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Item { Layout.fillWidth: true }
                PfButton {
                    objectName: "composeApplyButton"
                    text: root.bothSingles ? (L10n.translate("Combine moments", "Объединить моменты")) : (L10n.translate("Apply replacement", "Применить замену"))
                    enabled: !!root.donor && !!root.record && !root.sameMoment && !Analysis.busy && !Analysis.exportBusy && !Analysis.reviewBusy
                    onClicked: {
                        const accepted = root.bothSingles ? Analysis.combineSingleResults(root.record.id, root.donor.id) : Analysis.replaceResultSide(root.record.id, root.targetSide, root.donor.id, root.donorSide)
                        if (accepted) root.close()
                        else root.errorText = L10n.translate("Could not apply the replacement. Check that the selected moment is still available.", "Не удалось применить замену. Проверь, доступен ли выбранный момент.")
                    }
                }
            }
        }
    }
}
