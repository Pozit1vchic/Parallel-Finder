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
    property string sortCriterion: "movement"
    signal exportSelectionChanged(var rows)
    signal resultSelected(int index)
    signal exportRequested()
    implicitWidth: Theme.sidePanelWidth
    color: Theme.rail
    radius: Theme.radiusCard
    border.color: Theme.border
    property var visibleResults: []

    function movementSimilarity(item) {
        return Number(item.similarity || 0)
    }

    function selectAll() {
        const rows = {}
        for (const item of visibleResults) rows[item.id] = true
        exportSelectionChanged(rows)
    }

    function clearSelection() { exportSelectionChanged({}) }

    function sceneSimilarity(item) {
        return Number(item.sceneSimilarity !== undefined ? item.sceneSimilarity : 0)
    }

    function timeValue(item) {
        return Number(item.leftStart || 0)
    }

    function criterionLabel() {
        if (sortCriterion === "scene") return L10n.t("results.sceneSimilarity")
        if (sortCriterion === "time") return L10n.t("results.time")
        return L10n.t("results.score")
    }

    function compareValues(a, b) {
        let delta = 0
        if (sortCriterion === "scene")
            delta = sceneSimilarity(a) - sceneSimilarity(b)
        else if (sortCriterion === "time")
            delta = timeValue(a) - timeValue(b)
        else
            delta = movementSimilarity(a) - movementSimilarity(b)
        if (Math.abs(delta) > 1e-9)
            return delta * (sortDescending ? -1 : 1)
        return (Number(a.id) - Number(b.id))
    }

    function rebuild() {
        const next = []
        for (const item of (results || []))
            next.push(item)
        next.sort(compareValues)
        visibleResults = next
        Qt.callLater(revealSelection)
    }

    function revealSelection() {
        const index = visibleResults.findIndex(function(item) { return Number(item.id) === root.selectedIndex })
        resultList.currentIndex = index
        if (index >= 0)
            resultList.positionViewAtIndex(index, ListView.Contain)
    }

    function selectPrevious() {
        if (!visibleResults.length) return
        const current = visibleResults.findIndex(function(item) { return Number(item.id) === root.selectedIndex })
        const target = current <= 0 ? visibleResults[visibleResults.length - 1] : visibleResults[current - 1]
        root.resultSelected(Number(target.id))
    }

    function selectNext() {
        if (!visibleResults.length) return
        const current = visibleResults.findIndex(function(item) { return Number(item.id) === root.selectedIndex })
        const target = current < 0 || current >= visibleResults.length - 1 ? visibleResults[0] : visibleResults[current + 1]
        root.resultSelected(Number(target.id))
    }

    onSelectedIndexChanged: revealSelection()
    onResultsChanged: rebuild()
    onSortDescendingChanged: rebuild()
    onSortCriterionChanged: rebuild()
    Component.onCompleted: rebuild()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            visible: root.visibleResults.length > 0
            spacing: 6
            PfButton { Layout.fillWidth: true; text: L10n.t("results.selectAll"); onClicked: root.selectAll() }
            PfButton { Layout.fillWidth: true; text: L10n.t("results.clearSelection"); onClicked: root.clearSelection() }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4
            Text {
                font.family: Theme.fontFamily
                Layout.fillWidth: true
                text: L10n.t("results.title")
                color: Theme.textPrimary
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                font.family: Theme.fontFamily
                Layout.fillWidth: true
                text: root.results.length > 0 ? root.results.length + " " + L10n.t("results.found") : L10n.t("results.emptyHint")
                color: Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: root.visibleResults.length > 0 ? 28 : 0
            visible: root.visibleResults.length > 0
            spacing: 6
            PfIconButton {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                iconSource: "qrc:/qt/qml/PfUi/qml/assets/chevron-right.svg"
                iconRotation: -90
                accessibleName: L10n.t("results.previous")
                enabled: root.visibleResults.length > 0
                onClicked: root.selectPrevious()
            }
            Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
            PfIconButton {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                iconSource: "qrc:/qt/qml/PfUi/qml/assets/chevron-right.svg"
                iconRotation: 90
                accessibleName: L10n.t("results.next")
                enabled: root.visibleResults.length > 0
                onClicked: root.selectNext()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.visibleResults.length > 0 ? 1 : 0
            visible: root.visibleResults.length > 0
            color: Theme.hairline
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: root.visibleResults.length > 0 ? implicitHeight : 0
            visible: root.visibleResults.length > 0
            spacing: 6

            Text {
                font.family: Theme.fontFamily
                text: L10n.t("results.sortBy")
                color: Theme.textSecondary
                font.pixelSize: 10
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                PfButton {
                    Layout.fillWidth: true
                    text: L10n.t("results.score")
                    quiet: root.sortCriterion !== "movement"
                    onClicked: root.sortCriterion = "movement"
                }
                PfButton {
                    Layout.fillWidth: true
                    text: L10n.t("results.scene")
                    quiet: root.sortCriterion !== "scene"
                    onClicked: root.sortCriterion = "scene"
                }
                PfButton {
                    Layout.fillWidth: true
                    text: L10n.t("results.time")
                    quiet: root.sortCriterion !== "time"
                    onClicked: root.sortCriterion = "time"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                PfButton {
                    Layout.fillWidth: true
                    text: root.criterionLabel() + (root.sortDescending ? " ↓" : " ↑")
                    quiet: true
                    onClicked: root.sortDescending = !root.sortDescending
                }
                PfButton {
                    Layout.fillWidth: true
                    text: L10n.t("results.export")
                    enabled: Object.keys(root.selectedRows).length > 0
                    quiet: Object.keys(root.selectedRows).length === 0
                    onClicked: root.exportRequested()
                }
            }
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
                height: Math.max(96, rowContent.implicitHeight + 16)
                radius: 7
                color: root.selectedRows[modelData.id] ? Theme.accentMuted : Theme.surfaceRaised
                border.color: Number(modelData.id) === root.selectedIndex ? Theme.accent : Theme.border
                Accessible.name: L10n.matchLabel(modelData) + " " + Math.round(root.movementSimilarity(modelData) * 100) + "%"
                Accessible.role: Accessible.ListItem

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.resultSelected(modelData.id)
                }

                Row {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    PfCheckBox {
                        width: 18
                        text: ""
                        checked: root.selectedRows[modelData.id] === true
                        Accessible.name: L10n.t("results.export") + " " + (modelData.id + 1)
                        onToggled: {
                            const next = Object.assign({}, root.selectedRows)
                            if (checked) next[modelData.id] = true
                            else delete next[modelData.id]
                            root.exportSelectionChanged(next)
                        }
                    }

                    Column {
                        id: rowContent
                        width: Math.max(0, parent.width - 34)
                        spacing: 4

                        Text {
                            font.family: Theme.fontFamily
                            width: parent.width
                            text: L10n.matchLabel(modelData) + " · " + Math.round(root.movementSimilarity(modelData) * 100) + "%"
                            color: Theme.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            font.family: Theme.fontFamily
                            width: parent.width
                            text: L10n.t("results.sceneSimilarity") + " · " + Math.round(root.sceneSimilarity(modelData) * 100) + "%"
                            color: Theme.sage
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                        Text {
                            font.family: Theme.fontFamily
                            width: parent.width
                            text: L10n.t("results.file") + " · " + String(modelData.leftSource || "").split(/[\\/]/).pop() + "  ↔  " + String(modelData.rightSource || "").split(/[\\/]/).pop()
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        Text {
                            font.family: Theme.fontFamily
                            width: parent.width
                            text: L10n.t("results.time") + " · " + root.formatTime(modelData.leftStart) + "  /  " + root.formatTime(modelData.rightStart)
                            color: Theme.textDisabled
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Text {
                font.family: Theme.fontFamily
                anchors.centerIn: parent
                visible: root.visibleResults.length === 0
                width: parent.width - 28
                text: root.results.length === 0 ? L10n.t("results.emptyBody") : L10n.t("results.emptyHint")
                color: Theme.textDisabled
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                lineHeight: 1.25
            }
        }
    }

    function formatTime(seconds) {
        const total = Math.max(0, Number(seconds) || 0)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = Math.floor(total % 60)
        return [h, m, s].map(function(v) { return String(v).padStart(2, "0") }).join(":")
    }
}
