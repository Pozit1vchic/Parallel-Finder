import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import PfUi
import PfUiBridge

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
    signal exportRequested(var rows)
    property string classificationFilter: ""
    property string categoryFilter: ""
    property string colorFilter: ""
    readonly property var classificationKeys: [...new Set((results || []).map(function(item) { return String(item.classification || "") }).filter(function(key) { return key.length > 0 }))].sort()
    readonly property var categoryKeys: [...new Set((results || []).map(function(item) { return String(item.category || "") }).filter(function(key) { return key.length > 0 }))].sort()
    readonly property var groupColors: ["", "#D56565", "#638EDB", "#7C9885", "#AA83D4", "#D5AD63"]
    function exportRows() {
        const rows = {}
        for (const item of visibleResults) if (selectedRows[item.id] === true) rows[item.id] = true
        return rows
    }
    function applyCategory(name, color) {
        const ids = selectedCount > 0 ? visibleResults.filter(function(item) { return selectedRows[item.id] === true }).map(function(item) { return Number(item.id) }) : [selectedIndex]
        // Snapshot IDs before category edits rebuild the filtered list.
        for (const id of ids) if (id >= 0) Analysis.setResultCategory(id, name, color)
    }
    implicitWidth: Theme.sidePanelWidth
    color: Theme.rail
    radius: Theme.radiusCard
    border.color: Theme.border
    property var visibleResults: []
    readonly property int selectedCount: visibleResults.filter(function(item) { return selectedRows[item.id] === true }).length
    readonly property int currentVisibleIndex: visibleResults.findIndex(function(item) { return Number(item.id) === root.selectedIndex })
    function goToTop() {
        if (!visibleResults.length) return
        root.resultSelected(Number(visibleResults[0].id))
        resultList.positionViewAtBeginning()
    }

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
        for (const item of (results || [])) {
            if (classificationFilter && String(item.classification || "") !== classificationFilter) continue
            if (categoryFilter === "__untagged__" && item.category) continue
            if (categoryFilter && categoryFilter !== "__untagged__" && item.category !== categoryFilter) continue
            if (colorFilter && String(item.categoryColor || "").toLowerCase() !== colorFilter.toLowerCase()) continue
            next.push(item)
        }
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
        if (current <= 0) return
        const target = visibleResults[current - 1]
        root.resultSelected(Number(target.id))
    }

    function selectNext() {
        if (!visibleResults.length) return
        const current = visibleResults.findIndex(function(item) { return Number(item.id) === root.selectedIndex })
        if (current >= visibleResults.length - 1) return
        const target = visibleResults[current < 0 ? 0 : current + 1]
        root.resultSelected(Number(target.id))
    }

    onSelectedIndexChanged: revealSelection()
    onResultsChanged: rebuild()
    onClassificationFilterChanged: rebuild()
    onCategoryFilterChanged: rebuild()
    onColorFilterChanged: rebuild()
    onClassificationKeysChanged: if (classificationFilter && classificationKeys.indexOf(classificationFilter) < 0) classificationFilter = ""
    onCategoryKeysChanged: if (categoryFilter && categoryFilter !== "__untagged__" && categoryKeys.indexOf(categoryFilter) < 0) categoryFilter = ""
    onSortDescendingChanged: rebuild()
    onSortCriterionChanged: rebuild()
    Component.onCompleted: rebuild()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Flickable {
            id: controlsFlick
            objectName: "resultControlsFlick"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(controlsColumn.implicitHeight, Math.max(120,root.height-180))
            contentWidth: width; contentHeight: controlsColumn.implicitHeight; clip: true
            onContentHeightChanged: if (categoryEditor.expanded) contentY = Math.max(0,contentHeight-height)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: controlsColumn; width: parent.width; spacing: 8
        RowLayout {
            Layout.fillWidth: true
            visible: root.results.length > 0
            spacing: 6
            PfButton { objectName: "selectAllResultsButton"; Layout.fillWidth: true; quiet: true; enabled: root.selectedCount < root.visibleResults.length; text: L10n.t("results.selectAll"); onClicked: root.selectAll() }
            PfButton { objectName: "clearResultsSelectionButton"; Layout.fillWidth: true; quiet: true; enabled: root.selectedCount > 0; text: L10n.t("results.clearSelection"); onClicked: root.clearSelection() }
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
            Layout.preferredHeight: root.results.length > 0 ? 28 : 0
            visible: root.results.length > 0
            spacing: 6
            PfIconButton {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                iconSource: "qrc:/qt/qml/PfUi/qml/assets/chevron-right.svg"
                iconRotation: -90
                accessibleName: L10n.t("results.previous")
                enabled: root.currentVisibleIndex > 0
                onClicked: root.selectPrevious()
            }
            Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
            PfIconButton {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                iconSource: "qrc:/qt/qml/PfUi/qml/assets/chevron-right.svg"
                iconRotation: 90
                accessibleName: L10n.t("results.next")
                enabled: root.currentVisibleIndex < root.visibleResults.length - 1
                onClicked: root.selectNext()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.results.length > 0 ? 1 : 0
            visible: root.results.length > 0
            color: Theme.hairline
        }

        Text {
            Layout.fillWidth: true
            visible: root.results.length > 0
            text: L10n.t("results.sortBy")
            font.family: Theme.fontFamily
            font.pixelSize: 10
            color: Theme.textSecondary
        }
        PfComboBox {
            id: resultSort
            objectName: "resultSortCombo"
            // This choice is mouse-only; arrows belong to result navigation.
            Keys.onPressed: function(event) {
                if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) < 0) return
                if (popup.visible) popup.close()
                resultList.forceActiveFocus()
                if (event.key === Qt.Key_Up) root.selectPrevious()
                if (event.key === Qt.Key_Down) root.selectNext()
                event.accepted = true
            }
            Layout.fillWidth: true
            visible: root.results.length > 0
            Accessible.name: L10n.t("results.sortBy")
            model: [L10n.t("results.scoreHigh"), L10n.t("results.scoreLow"),
                    L10n.t("results.sceneHigh"), L10n.t("results.sceneLow"),
                    L10n.t("results.timeEarly"), L10n.t("results.timeLate")]
            currentIndex: root.sortCriterion === "time" ? (root.sortDescending ? 5 : 4)
                : root.sortCriterion === "scene" ? (root.sortDescending ? 2 : 3)
                : (root.sortDescending ? 0 : 1)
            onActivated: function(index) {
                root.sortCriterion = index >= 4 ? "time" : index >= 2 ? "scene" : "movement"
                root.sortDescending = index < 4 ? index % 2 === 0 : index === 5
                resultList.forceActiveFocus()
            }
        }
        RowLayout {
            Layout.fillWidth: true; visible: root.results.length > 0; spacing: 6
            PfComboBox {
                objectName: "classificationFilterCombo"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                model: [L10n.t("results.allClasses")].concat(root.classificationKeys.map(L10n.classificationLabel))
                currentIndex: Math.max(0,root.classificationKeys.indexOf(root.classificationFilter)+1)
                Accessible.name: L10n.t("results.classification")
                onActivated: function(index) { root.classificationFilter = index > 0 ? root.classificationKeys[index-1] : "" }
            }
        }
        PfComboBox {
            objectName: "categoryFilterCombo"
            Layout.fillWidth: true; visible: root.results.length > 0
            model: [L10n.t("results.allCategories"),L10n.t("results.untagged")].concat(root.categoryKeys)
            currentIndex: root.categoryFilter === "__untagged__" ? 1 : root.categoryFilter ? Math.max(0,root.categoryKeys.indexOf(root.categoryFilter)+2) : 0
            Accessible.name: L10n.t("results.category")
            onActivated: function(index) { root.categoryFilter = index === 1 ? "__untagged__" : index > 1 ? root.categoryKeys[index-2] : "" }
        }
        PfComboBox {
            objectName: "colorFilterCombo"
            Layout.fillWidth: true; visible: root.results.length > 0
            model: [L10n.t("results.allColors"),L10n.t("colors.orange"),L10n.t("colors.blue"),L10n.t("colors.green"),L10n.t("colors.purple"),L10n.t("colors.gold")]
            currentIndex: Math.max(0,root.groupColors.indexOf(root.colorFilter))
            Accessible.name: L10n.t("results.categoryColor")
            onActivated: function(index) { root.colorFilter = root.groupColors[index] }
        }
        CollapsibleSection {
            id: categoryEditor
            objectName: "resultCategoryEditor"
            Layout.fillWidth: true; visible: root.results.length > 0
            title: L10n.t("results.tagSelection")
            onToggled: function(value) {
                expanded = value
                if (value) Qt.callLater(function() { controlsFlick.contentY = Math.max(0,controlsFlick.contentHeight-controlsFlick.height) })
            }
            Column {
                width: parent.width; spacing: 6
                PfTextField { id: categoryName; width: parent.width; placeholderText: L10n.t("results.categoryName"); maximumLength: 64; Accessible.name: placeholderText }
                PfComboBox {
                    id: categoryColor; objectName: "categoryColorCombo"; width: parent.width
                    model: [L10n.t("results.removeTag"),L10n.t("colors.orange"),L10n.t("colors.blue"),L10n.t("colors.green"),L10n.t("colors.purple"),L10n.t("colors.gold")]
                    currentIndex: 1
                    Accessible.name: L10n.t("results.categoryColor")
                }
                PfButton {
                    objectName: "applyCategoryButton"; width: parent.width; text: L10n.t("results.applyTag")
                    enabled: !Analysis.busy && (root.selectedCount > 0 || root.selectedIndex >= 0)
                    onClicked: root.applyCategory(categoryColor.currentIndex === 0 ? "" : categoryName.text.trim() || categoryColor.currentText, root.groupColors[categoryColor.currentIndex])
                }
            }
        }
        PfButton {
            Layout.fillWidth: true
            visible: root.results.length > 0
            text: L10n.t("results.export") + (root.selectedCount > 0 ? " · " + root.selectedCount : "")
            enabled: root.selectedCount > 0
            primary: enabled
            onClicked: root.exportRequested(root.exportRows())
        }

            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
        ListView {
            id: resultList
            anchors.fill: parent
            objectName: "resultList"
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationWraps: false
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
                id: resultCard
                property real entranceOffset: 0
                transform: Translate { y: resultCard.entranceOffset }
                Component.onCompleted: {
                    // Only six first-visible cards, never thousands of rows.
                    if (index < 6 && !Theme.reducedMotion) {
                        opacity = 0; entranceOffset = 6; cardReveal.start()
                    }
                }
                SequentialAnimation {
                    id: cardReveal
                    PauseAnimation { duration: Theme.reducedMotion ? 0 : Math.max(0, index) * 18 }
                    ParallelAnimation {
                        NumberAnimation { target: resultCard; property: "opacity"; to: 1; duration: Theme.motionRevealDuration; easing.type: Easing.OutCubic }
                        NumberAnimation { target: resultCard; property: "entranceOffset"; to: 0; duration: Theme.motionRevealDuration; easing.type: Easing.OutCubic }
                    }
                }
                Connections {
                    target: Theme
                    function onReducedMotionChanged() {
                        if (!Theme.reducedMotion) return
                        cardReveal.stop(); resultCard.opacity = 1; resultCard.entranceOffset = 0
                    }
                }
                width: Math.max(0, resultList.width - 8)
                height: Math.max(96, rowContent.implicitHeight + 16)
                radius: 7
                color: root.selectedRows[modelData.id] ? Theme.accentMuted : Theme.surfaceRaised
                border.color: Number(modelData.id) === root.selectedIndex ? Theme.accent : Theme.border
                Behavior on border.color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                Rectangle {
                    x: 0; y: 14; width: 2; height: parent.height - 28; radius: 1
                    color: modelData.categoryColor || Theme.accent
                    opacity: modelData.category ? 1 : Number(modelData.id) === root.selectedIndex ? 1 : 0
                    Behavior on opacity { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration } }
                }
                Accessible.name: L10n.matchLabel(modelData) + " " + Math.round(root.movementSimilarity(modelData) * 100) + "%"
                Accessible.role: Accessible.ListItem

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { resultList.forceActiveFocus(); root.resultSelected(modelData.id) }
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
                            width: parent.width; font.family: Theme.fontFamily; font.pixelSize: 10
                            text: L10n.classificationLabel(String(modelData.classification || ""))
                            color: Theme.textSecondary; elide: Text.ElideRight
                        }
                        Text {
                            visible: !!modelData.category; width: parent.width; font.family: Theme.fontFamily; font.pixelSize: 10
                            text: "●  " + (modelData.category || ""); color: modelData.categoryColor || Theme.accent; elide: Text.ElideRight
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
        PfButton {
            objectName: "resultsToTopButton"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            compact: true
            text: "↑ " + L10n.t("results.toTop")
            opacity: resultList.contentY > 32 ? 1 : 0
            scale: 0.94 + 0.06 * opacity
            visible: opacity > 0
            enabled: resultList.contentY > 32
            Behavior on opacity { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic } }
            onClicked: root.goToTop()
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
