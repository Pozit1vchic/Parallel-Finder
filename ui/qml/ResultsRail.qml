import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import PfUi

Rectangle {
    id: root
    objectName: "resultsRail"
    property var results: []
    property var selectedRows: ({})
    property bool sortDescending: true
    property int selectedIndex: -1
    signal exportSelectionChanged(var rows)
    signal resultSelected(int index)
    signal exportRequested()
    implicitWidth: Theme.sidePanelWidth; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
    property var visibleResults: []

    function rebuild() {
        const next = []
        for (const item of (results || [])) next.push(item)
        next.sort(function (a, b) { return (Number(a.similarity) - Number(b.similarity)) * (sortDescending ? -1 : 1) })
        visibleResults = next
        Qt.callLater(revealSelection)
    }
    function selectPrevious() {
        if (!visibleResults.length) return
        const current = visibleResults.findIndex(function (item) { return Number(item.id) === root.selectedIndex })
        const target = current <= 0 ? visibleResults[visibleResults.length - 1] : visibleResults[current - 1]
        root.resultSelected(Number(target.id))
    }
    function selectNext() {
        if (!visibleResults.length) return
        const current = visibleResults.findIndex(function (item) { return Number(item.id) === root.selectedIndex })
        const target = current < 0 || current >= visibleResults.length - 1 ? visibleResults[0] : visibleResults[current + 1]
        root.resultSelected(Number(target.id))
    }
    function movementLabel(item) {
        if (item.headOnlyComparison === true) return "Похожее положение головы"
        if (item.matchType === "pose" || (item.direction === "static" && item.gesture === "static"))
            return "похожая поза"
        const labels = []
        const direction = String(item.direction || "").trim()
        const gesture = String(item.gesture || "").trim()
        if (direction && direction.toLowerCase() !== "mixed") labels.push(direction)
        if (gesture && gesture.toLowerCase() !== "mixed" && gesture !== direction) labels.push(gesture)
        return labels.length ? labels.join(" · ") : "движение"
    }
    function revealSelection() {
        const index = visibleResults.findIndex(function(item) { return Number(item.id) === root.selectedIndex })
        resultList.currentIndex = index
        if (index >= 0) resultList.positionViewAtIndex(index, ListView.Contain)
    }
    onSelectedIndexChanged: revealSelection()
    onResultsChanged: rebuild()
    onSortDescendingChanged: rebuild()
    Component.onCompleted: rebuild()

    ColumnLayout { anchors.fill: parent; anchors.margins: 16; spacing: 12
        ColumnLayout { Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 4
            Text { font.family: Theme.fontFamily; Layout.fillWidth: true; text: L10n.t("results.title"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold; elide: Text.ElideRight }
            Text { font.family: Theme.fontFamily; Layout.fillWidth: true; text: root.results.length > 0 ? root.results.length + " " + L10n.t("results.found") : L10n.t("results.emptyHint"); color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight }
        }
        RowLayout { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: root.visibleResults.length > 0 ? 28 : 0; visible: root.visibleResults.length > 0; spacing: 6
            PfIconButton { Layout.preferredWidth: 28; Layout.preferredHeight: 28; iconSource: "qrc:/qt/qml/PfUi/qml/assets/chevron-right.svg"; iconRotation: -90; accessibleName: L10n.t("results.previous"); enabled: root.visibleResults.length > 0; onClicked: root.selectPrevious() }
            Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
            PfIconButton { Layout.preferredWidth: 28; Layout.preferredHeight: 28; iconSource: "qrc:/qt/qml/PfUi/qml/assets/chevron-right.svg"; iconRotation: 90; accessibleName: L10n.t("results.next"); enabled: root.visibleResults.length > 0; onClicked: root.selectNext() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: root.visibleResults.length > 0 ? 1 : 0; visible: root.visibleResults.length > 0; color: Theme.hairline }
        RowLayout { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: root.visibleResults.length > 0 ? 46 : 0; visible: root.visibleResults.length > 0; spacing: 6
            PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.sortDescending ? L10n.t("results.sort") + " ↓" : L10n.t("results.sort") + " ↑"; quiet: true; onClicked: root.sortDescending = !root.sortDescending }
            PfButton { Layout.fillWidth: true; Layout.minimumWidth: 0; text: L10n.t("results.export"); enabled: Object.keys(root.selectedRows).length > 0; quiet: Object.keys(root.selectedRows).length === 0; onClicked: root.exportRequested() }
        }
        ListView {
            id: resultList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.minimumHeight: 80
            clip: true
            spacing: 5
            model: root.visibleResults
            focus: true
            activeFocusOnTab: true
            Accessible.name: L10n.t("results.title")
            Keys.onUpPressed: { root.selectPrevious(); event.accepted = true }
            Keys.onDownPressed: { root.selectNext(); event.accepted = true }
            Keys.onReturnPressed: if (root.visibleResults.length > 0) root.resultSelected(root.selectedIndex < 0 ? Number(root.visibleResults[0].id) : root.selectedIndex)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                width: Math.max(0, resultList.width - 8)
                height: Math.max(78, rowContent.implicitHeight + 14)
                radius: 7
                color: root.selectedRows[modelData.id] ? Theme.accentMuted : Theme.surfaceRaised
                border.color: Number(modelData.id) === root.selectedIndex ? Theme.accent : Theme.border
                Accessible.name: root.movementLabel(modelData)
                Accessible.role: Accessible.ListItem
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.resultSelected(modelData.id)
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 700
                    ToolTip.text: "Кандидат для просмотра. Проценты точности не определены: оценка алгоритма не откалибрована на размеченной выборке. "
                        + ((modelData.matchType === "pose" || modelData.gesture === "static")
                           ? "Сравнивается поза; повторение движения не подтверждено."
                           : "Сравниваются траектории движения во времени.")
                }
                Row { anchors.fill: parent; anchors.margins: 7; spacing: 8
                    PfCheckBox { id: exportCheck; width: 18; text: ""; checked: root.selectedRows[modelData.id] === true; Accessible.name: L10n.t("results.export") + " " + (modelData.id + 1); onToggled: { const next = Object.assign({}, root.selectedRows); if (checked) next[modelData.id] = true; else delete next[modelData.id]; root.exportSelectionChanged(next) } }
                    Column { id: rowContent; width: Math.max(0, parent.width - 34); spacing: 3
                        Text { font.family: Theme.fontFamily; width: parent.width; text: root.movementLabel(modelData); color: Theme.textPrimary; font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 2; clip: true }
                        Text { font.family: Theme.fontFamily; width: parent.width; text: String(modelData.leftSource || "").split(/[\\/]/).pop() + "  ↔  " + String(modelData.rightSource || "").split(/[\\/]/).pop(); color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideMiddle; clip: true }
                        Text { font.family: Theme.fontFamily; width: parent.width; text: root.formatTime(modelData.leftStart) + "  /  " + root.formatTime(modelData.rightStart); color: Theme.textDisabled; font.pixelSize: 11; elide: Text.ElideRight }
                        Text { font.family: Theme.fontFamily;
                            width: parent.width
                            visible: modelData.leftTrackId !== undefined || modelData.rightTrackId !== undefined
                            text: "трек A #" + String(modelData.leftTrackId || "?") + "  ·  трек B #" + String(modelData.rightTrackId || "?")
                            color: Theme.textDisabled
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                        Text { font.family: Theme.fontFamily;
                            width: parent.width
                            text: modelData.identityVerified === true
                                ? (modelData.costumeMode === true ? "Костюм · внешность похожа" : modelData.faceVerified === true ? "Лицо · признаки согласуются" : "ReID · внешность похожа")
                                : "ReID · недостаточно данных о внешности"
                            color: modelData.identityVerified === true ? Theme.sage : Theme.textDisabled
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            ToolTip.visible: reidHint.hovered
                            ToolTip.delay: 350
                            ToolTip.text: "ReID сравнивает внешний вид тела, а не подтверждает личность."
                            HoverHandler { id: reidHint }
                        }
                    }
                }
            }
            Text { font.family: Theme.fontFamily; anchors.centerIn: parent; visible: root.visibleResults.length === 0; width: parent.width - 28; text: root.results.length === 0 ? L10n.t("results.emptyBody") : L10n.t("results.emptyHint"); color: Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap; font.pixelSize: 12; lineHeight: 1.25 }
        }
    }

    function formatTime(seconds) {
        const total = Math.max(0, Number(seconds) || 0); const h = Math.floor(total / 3600); const m = Math.floor((total % 3600) / 60); const s = Math.floor(total % 60)
        return [h, m, s].map(function(v) { return String(v).padStart(2, "0") }).join(":")
    }
}
