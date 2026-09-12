import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi

Rectangle {
    id: root
    property var results: []
    property var selectedRows: ({})
    property string activeFilter: "all"
    property bool sortDescending: true
    property int selectedIndex: -1
    signal exportSelectionChanged(var rows)
    signal resultSelected(int index)
    signal exportRequested()
    width: Theme.sidePanelWidth; color: Theme.rail; radius: Theme.radiusCard; border.color: Theme.border
    layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowPanel; shadowBlur: 0.75; shadowVerticalOffset: 10 }
    property var visibleResults: []

    function matchesFilter(item) {
        if (activeFilter === "all") return true
        const direction = String(item.direction || "").toLowerCase()
        if (activeFilter === "forward") return direction.indexOf("toward") >= 0 || direction.indexOf("camera") >= 0
        if (activeFilter === "side") return direction === "left" || direction === "right"
        return true
    }
    function rebuild() {
        const next = []
        for (const item of (results || [])) if (matchesFilter(item)) next.push(item)
        next.sort(function (a, b) { return (Number(a.similarity) - Number(b.similarity)) * (sortDescending ? -1 : 1) })
        visibleResults = next
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
    onResultsChanged: rebuild()
    onActiveFilterChanged: rebuild()
    onSortDescendingChanged: rebuild()
    Component.onCompleted: rebuild()

    Column { anchors.fill: parent; anchors.margins: 16; spacing: 12
        Row { width: parent.width
            Column { width: parent.width - 36; spacing: 4
                Text { text: L10n.t("results.title"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text { text: root.results.length > 0 ? root.results.length + " " + L10n.t("results.found") : L10n.t("results.emptyHint"); color: Theme.textSecondary; font.pixelSize: 11 }
            }
            Text { text: root.results.length; color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 20 }
        }
        Row { width: parent.width; spacing: 6
            PfIconButton { width: 28; height: 28; iconSource: "qrc:/qt/qml/PfUi/assets/chevron-left.svg"; accessibleName: L10n.t("results.previous"); enabled: root.visibleResults.length > 0; onClicked: root.selectPrevious() }
            Text { width: parent.width - 68; text: root.results.length > 0 ? L10n.t("results.selectPair") : L10n.t("common.empty"); color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
            PfIconButton { width: 28; height: 28; iconSource: "qrc:/qt/qml/PfUi/assets/chevron-right.svg"; accessibleName: L10n.t("results.next"); enabled: root.visibleResults.length > 0; onClicked: root.selectNext() }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
        Row { width: parent.width; spacing: 6
            PfButton { width: (parent.width - 6) / 2; text: root.sortDescending ? L10n.t("results.sort") + " ↓" : L10n.t("results.sort") + " ↑"; quiet: true; onClicked: root.sortDescending = !root.sortDescending }
            PfButton { width: (parent.width - 6) / 2; text: L10n.t("results.export"); enabled: Object.keys(root.selectedRows).length > 0; quiet: Object.keys(root.selectedRows).length === 0; onClicked: root.exportRequested() }
        }
        Text { text: L10n.t("results.filters"); color: Theme.textSecondary; font.pixelSize: 11 }
        Row { width: parent.width; spacing: 5
            PfButton { width: (parent.width - 10) / 3; text: L10n.t("results.all"); quiet: root.activeFilter !== "all"; onClicked: root.activeFilter = "all" }
            PfButton { width: (parent.width - 10) / 3; text: L10n.t("results.forward"); quiet: root.activeFilter !== "forward"; onClicked: root.activeFilter = "forward" }
            PfButton { width: (parent.width - 10) / 3; text: L10n.t("results.side"); quiet: root.activeFilter !== "side"; onClicked: root.activeFilter = "side" }
        }
        ListView {
            id: resultList; width: parent.width; height: parent.height - 178; clip: true; spacing: 5; model: root.visibleResults; focus: true; activeFocusOnTab: true
            Accessible.name: L10n.t("results.title")
            Keys.onUpPressed: { root.selectPrevious(); event.accepted = true }
            Keys.onDownPressed: { root.selectNext(); event.accepted = true }
            Keys.onReturnPressed: if (root.visibleResults.length > 0) root.resultSelected(root.selectedIndex < 0 ? Number(root.visibleResults[0].id) : root.selectedIndex)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                width: resultList.width - 8; height: Theme.resultRowHeight + 12; radius: 7; color: root.selectedRows[modelData.id] ? Theme.accentMuted : Theme.surfaceRaised; border.color: index === root.selectedIndex ? Theme.accent : Theme.border; Accessible.name: String(modelData.direction || "") + " " + Math.round(Number(modelData.similarity) * 100) + "%"; Accessible.role: Accessible.ListItem
                MouseArea { anchors.fill: parent; onClicked: root.resultSelected(modelData.id) }
                Row { anchors.fill: parent; anchors.margins: 7; spacing: 8
                    PfCheckBox { id: exportCheck; width: 18; text: ""; checked: root.selectedRows[modelData.id] === true; Accessible.name: L10n.t("results.export") + " " + (modelData.id + 1); onToggled: { const next = Object.assign({}, root.selectedRows); if (checked) next[modelData.id] = true; else delete next[modelData.id]; root.exportSelectionChanged(next) } }
                    Column { width: parent.width - 34; spacing: 2
                        Text { width: parent.width; text: Math.round(Number(modelData.similarity) * 100) + "%  ·  " + String(modelData.direction || "") + "  ·  " + String(modelData.gesture || ""); color: Theme.textPrimary; font.pixelSize: 10; elide: Text.ElideRight }
                        Text { width: parent.width; text: String(modelData.leftSource || "").split(/[\\/]/).pop() + "  ↔  " + String(modelData.rightSource || "").split(/[\\/]/).pop(); color: Theme.textSecondary; font.pixelSize: 9; elide: Text.ElideRight }
                        Text { width: parent.width; text: root.formatTime(modelData.leftStart) + "  /  " + root.formatTime(modelData.rightStart); color: Theme.textDisabled; font.pixelSize: 9 }
                    }
                }
            }
            Text { anchors.centerIn: parent; visible: root.visibleResults.length === 0; width: parent.width - 28; text: root.results.length === 0 ? L10n.t("results.emptyBody") : L10n.t("results.emptyHint"); color: Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; font.pixelSize: 11 }
        }
    }

    function formatTime(seconds) {
        const total = Math.max(0, Number(seconds) || 0); const h = Math.floor(total / 3600); const m = Math.floor((total % 3600) / 60); const s = Math.floor(total % 60)
        return [h, m, s].map(function(v) { return String(v).padStart(2, "0") }).join(":")
    }
}
