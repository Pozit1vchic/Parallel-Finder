import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi
import PfUiBridge

ReviewDialog {
    id: root
    objectName: "pairComposeDialog"
    title: L10n.t("ui.346")
    property var record: null
    property var candidates: []
    property string errorText: ""
    readonly property var donor: candidates[donorChoice.currentIndex] || null
    readonly property string targetSide: targetChoice.currentIndex === 0 ? "left" : "right"
    readonly property string donorSide: donorSideChoice.currentIndex === 0 ? "left" : "right"
    readonly property bool donorRemoved: !!donor && !!donor[donorSide + "Removed"]
    readonly property string donorPreview: donor && !donorRemoved ? donor[donorSide + "Preview"] || "" : ""
    readonly property bool bothSingles: !!record && !!donor && (Boolean(record.leftRemoved) !== Boolean(record.rightRemoved)) && (Boolean(donor.leftRemoved) !== Boolean(donor.rightRemoved))
    property bool preparing: false

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

    function refreshCurrentRecords() {
        if (!visible || !record) return
        const current = Analysis.results.find(function(row) { return row.id === root.record.id })
        if (!current || current.hidden) { close(); return }
        const donorId = donor ? donor.id : -1
        record = current
        candidates = Analysis.results.filter(function(row) { return row.id !== current.id && !row.hidden })
        const previous = candidates.findIndex(function(row) { return row.id === donorId })
        donorChoice.currentIndex = previous >= 0 ? previous : (candidates.length ? 0 : -1)
    }
    function reconcileSides() {
        if (record && (Boolean(record.leftRemoved) !== Boolean(record.rightRemoved)))
            targetChoice.currentIndex = record.leftRemoved ? 0 : 1
        if (donor && (Boolean(donor.leftRemoved) !== Boolean(donor.rightRemoved)))
            donorSideChoice.currentIndex = donor.leftRemoved ? 1 : 0
    }
    function animateTransfer() {
        if (bothSingles) {
            targetChoice.currentIndex = record.leftRemoved ? 0 : 1
            donorSideChoice.currentIndex = donor.leftRemoved ? 1 : 0
        }
        errorText = ""
        transfer.stop()
        flight.visible = false
        if (!visible || preparing || Theme.reducedMotion || !donorPreview) return
        const card = pairCards.itemAt(targetChoice.currentIndex)
        if (!card || donorImage.width <= 0 || card.width <= 0) return
        // Read both endpoints once; only the overlay's transform moves.
        const origin = donorImage.mapToItem(stage, 0, 0)
        const destination = card.imageArea.mapToItem(stage, 0, 0)
        flight.originX = origin.x
        flight.originY = origin.y
        flight.width = donorImage.width
        flight.height = donorImage.height
        flight.source = donorPreview
        flight.travelX = 0
        flight.travelY = 0
        stretch.xScale = 1
        stretch.yScale = 1
        flight.toX = destination.x - origin.x
        flight.toY = destination.y - origin.y
        flight.toScaleX = card.imageArea.width / donorImage.width
        flight.toScaleY = card.imageArea.height / donorImage.height
        flight.visible = true
        transfer.start()
    }
    onDonorChanged: Qt.callLater(function() { root.reconcileSides(); root.animateTransfer() })
    onDonorPreviewChanged: Qt.callLater(animateTransfer)
    onTargetSideChanged: Qt.callLater(animateTransfer)
    onAboutToShow: {
        preparing = true
        errorText = ""
        candidates = Analysis.results.filter(function(row) { return record && row.id !== record.id && !row.hidden })
        targetChoice.currentIndex = 0
        donorSideChoice.currentIndex = 0
        donorChoice.currentIndex = candidates.length ? 0 : -1
        Qt.callLater(function() { root.reconcileSides(); root.preparing = false })
    }
    onAboutToHide: { transfer.stop(); flight.visible = false }
    onWidthChanged: { transfer.stop(); flight.visible = false }
    onHeightChanged: { transfer.stop(); flight.visible = false }
    Connections {
        target: Analysis
        function onResultsChanged() { if (root.visible) root.close() }
        function onResultCategoriesChanged() { Qt.callLater(root.refreshCurrentRecords) }
    }
    Connections {
        target: Theme
        function onReducedMotionChanged() { transfer.stop(); flight.visible = false }
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
                    Layout.fillWidth: true
                    Text { text: L10n.translate("Replace side", "Заменить сторону"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                    PfComboBox { id: targetChoice; objectName: "composeTargetChoice"; enabled: !root.bothSingles; Layout.fillWidth: true; model: [L10n.translate("Side A", "Сторона A"), L10n.translate("Side B", "Сторона B")] }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Text { text: L10n.translate("Source", "Источник"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                    PfComboBox {
                        id: donorChoice; objectName: "composeDonorChoice"; Layout.fillWidth: true
                        model: root.candidates.map(function(row) { return root.pairChoiceLabel(row) })
                    }
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
                        Layout.preferredWidth: 116; Layout.fillHeight: true
                        source: root.donorPreview; fillMode: Image.PreserveAspectFit
                        asynchronous: true; sourceSize.width: 640
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: L10n.translate("Moment", "Момент"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        PfComboBox {
                            id: donorSideChoice; objectName: "composeDonorSideChoice"; Layout.fillWidth: true; enabled: !root.bothSingles
                            model: root.donor ? [root.sideChoiceLabel(root.donor, "left", root.donor.leftRemoved), root.sideChoiceLabel(root.donor, "right", root.donor.rightRemoved)] : ["A", "B"]
                        }
                    }
                }
            }
            Text { text: L10n.translate("Pair after replacement", "Пара после замены"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 13 }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 72; spacing: 12
                Repeater {
                    id: pairCards
                    model: ["left", "right"]
                    delegate: Rectangle {
                        id: card
                        required property string modelData
                        property alias imageArea: preview
                        readonly property bool replaced: modelData === root.targetSide && !!root.donor
                        readonly property bool absent: replaced ? root.donorRemoved : (!!root.record && !!root.record[modelData + "Removed"])
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
                visible: !!root.errorText || !root.candidates.length
                text: root.errorText || (L10n.translate("Another result pair is needed for replacement.", "Для замены нужна ещё одна пара в результатах."))
                color: Theme.accentText; wrapMode: Text.WordWrap; font.family: Theme.fontFamily; font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Item { Layout.fillWidth: true }
                PfButton {
                    objectName: "composeApplyButton"
                    text: root.bothSingles ? (L10n.translate("Combine moments", "Объединить моменты")) : (L10n.translate("Apply replacement", "Применить замену"))
                    enabled: !!root.donor && !!root.record && !root.donorRemoved && !Analysis.busy && !Analysis.exportBusy && !Analysis.reviewBusy
                    onClicked: {
                        const accepted = root.bothSingles ? Analysis.combineSingleResults(root.record.id, root.donor.id) : Analysis.replaceResultSide(root.record.id, root.targetSide, root.donor.id, root.donorSide)
                        if (accepted) root.close()
                        else root.errorText = L10n.translate("Cannot replace this side. A moment cannot be paired with itself.", "Не удалось заменить сторону. Нельзя соединить момент с самим собой.")
                    }
                }
            }
        }
        Image {
            id: flight
            property real originX: 0
            property real originY: 0
            property real travelX: 0
            property real travelY: 0
            x: originX + travelX
            y: originY + travelY
            property real toX: 0
            property real toY: 0
            property real toScaleX: 1
            property real toScaleY: 1
            visible: false; z: 10
            fillMode: Image.PreserveAspectFit; sourceSize.width: 960
            transform: Scale { id: stretch; origin.x: 0; origin.y: 0 }
        }
        // One interruptible timeline communicates source → destination; no layout animation.
        SequentialAnimation {
            id: transfer
            ParallelAnimation {
                NumberAnimation { target: flight; property: "travelX"; to: flight.toX; duration: 320; easing.type: Easing.InOutCubic }
                NumberAnimation { target: flight; property: "travelY"; to: flight.toY; duration: 320; easing.type: Easing.InOutCubic }
                NumberAnimation { target: stretch; property: "xScale"; to: flight.toScaleX; duration: 320; easing.type: Easing.InOutCubic }
                NumberAnimation { target: stretch; property: "yScale"; to: flight.toScaleY; duration: 320; easing.type: Easing.InOutCubic }
            }
            PauseAnimation { duration: 40 }
            ScriptAction { script: flight.visible = false }
        }
    }
}
