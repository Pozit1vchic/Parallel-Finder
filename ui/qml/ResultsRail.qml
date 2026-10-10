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
    signal pairColorRequested(int index, string name, string color)
    signal clearColorsRequested(var ids)
    signal pairReviewRequested(int index, string field, bool value)
    signal findingsRequested()
    signal undoRequested()
    property string sourceQuery: ""
    property string sourceFilter: ""
    property string reviewNotice: ""
    readonly property var sourceChoices: {
        const sources = [""]
        for (const row of results) for (const side of ["left", "right"]) {
            const source = String(row[side + "Source"] || "")
            if (source && sources.indexOf(source) < 0) sources.push(source)
        }
        return [""].concat(sources.slice(1).sort())
    }
    function sourceName(path) { return String(path).split(/[\\/]/).pop() }
    function matchesSource(path) {
        return (!sourceFilter || String(path) === sourceFilter)
            && (!sourceQuery.trim() || sourceName(path).toLowerCase().indexOf(sourceQuery.trim().toLowerCase()) >= 0)
    }
    function selectNextUnreviewed() {
        const start = currentVisibleIndex
        for (let offset = 1; offset <= visibleResults.length; ++offset) {
            const index = (Math.max(-1, start) + offset) % visibleResults.length
            if (!visibleResults[index].reviewed) {
                reviewNotice = ""
                resultSelected(Number(visibleResults[index].id))
                return
            }
        }
        reviewNotice = Review.t("allReviewed")
    }
    property string reviewFilter: "all"
    property int selectionAnchorId: -1
    property var revealedCards: ({})
    ListModel { id: displayModel; dynamicRoles: true }
    readonly property var groupColors: ["", "#D56565", "#638EDB", "#7C9885", "#AA83D4", "#D5AD63"]
    readonly property var colorLabels: ["colors.none", "colors.orange", "colors.blue", "colors.green", "colors.purple", "colors.gold"]
    property int colorTargetId: -1
    function choosePairColor(id, paletteIndex) {
        const item = results.find(function(row) { return Number(row.id) === id })
        if (!item || paletteIndex < 0 || paletteIndex >= groupColors.length) return
        pairColorRequested(id, paletteIndex === 0 ? "" : L10n.t(colorLabels[paletteIndex]), groupColors[paletteIndex])
    }
    Popup {
        id: colorPicker
        objectName: "resultColorPicker"
        parent: Overlay.overlay
        padding: 8
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionDuration; easing.type: Easing.InCubic } }
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusButton; border.color: Theme.hairlineStrong }
        contentItem: Row {
            spacing: 6
            Repeater {
                model: root.groupColors
                delegate: Button {
                    required property int index
                    required property string modelData
                    objectName: "resultPaletteColor" + index
                    width: 28; height: 28
                    Accessible.name: L10n.t(root.colorLabels[index])
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    background: Rectangle { radius: 6; color: modelData || Theme.well; border.color: parent.hovered ? Theme.accent : Theme.hairlineStrong; border.width: 1 }
                    contentItem: Text { text: index === 0 ? "×" : ""; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: { root.choosePairColor(root.colorTargetId, index); colorPicker.close() }
                }
            }
        }
    }
    function exportRows() {
        const rows = {}
        for (const item of visibleResults) if (selectedRows[item.id] === true) rows[item.id] = true
        return rows
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
    readonly property var coloredSelectionIds: visibleResults.filter(function(row) {
        return !!row.categoryColor && (selectedCount === 0 || selectedRows[row.id] === true)
    }).map(function(row) { return Number(row.id) })
    function clearColors() { clearColorsRequested(coloredSelectionIds) }

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

    FontMetrics { id: resultTextMetrics; font.family: Theme.fontFamily; font.pixelSize: 11 }
    function layoutResults() { resultList.forceLayout() }
    function rebuild() {
        const previousRows = visibleResults
        const previousIndex = currentVisibleIndex
        const previousId = selectedIndex
        const previousY = resultList.contentY
        const next = []
        for (const item of (results || [])) {
            if (reviewFilter === "hidden" ? !item.hidden : item.hidden) continue
            if (reviewFilter === "favorites" && !item.favorite) continue
            if (!matchesSource(item.leftSource) && !matchesSource(item.rightSource)) continue
            next.push(item)
        }
        next.sort(compareValues)
        const wanted = {}; for (const item of next) wanted[item.id] = true
        for (let i = displayModel.count - 1; i >= 0; --i)
            if (!wanted[displayModel.get(i).entry.id]) displayModel.remove(i)
        for (let i = 0; i < next.length; ++i) {
            let existing = -1
            for (let j = i; j < displayModel.count; ++j)
                if (Number(displayModel.get(j).entry.id) === Number(next[i].id)) { existing = j; break }
            if (existing < 0) displayModel.insert(i, {entry: next[i]})
            else {
                if (existing !== i) displayModel.move(existing, i, 1)
                displayModel.set(i, {entry: next[i]})
            }
        }
        visibleResults = next
        if (next.some(function(row) { return !row.reviewed })) reviewNotice = ""
        if (previousId >= 0 && !next.some(function(row) { return Number(row.id) === previousId })) {
            const successor = previousRows.slice(Math.max(0, previousIndex + 1)).find(function(row) { return wanted[row.id] })
            const predecessor = previousRows.slice(0, Math.max(0, previousIndex)).reverse().find(function(row) { return wanted[row.id] })
            const target = successor || predecessor || next[0]
            resultSelected(target ? Number(target.id) : -1)
        }
        Qt.callLater(function() {
            resultList.forceLayout()
            resultList.contentY = Math.max(resultList.originY, Math.min(previousY, resultList.originY + Math.max(0, resultList.contentHeight - resultList.height)))
            revealSelection()
        })
    }
    function selectCard(id, modifiers) {
        if (Review.multiSelect) {
            let next = (modifiers & Qt.ControlModifier) ? Object.assign({}, selectedRows) : {}
            if (modifiers & Qt.ShiftModifier) {
                const from = visibleResults.findIndex(function(row) { return Number(row.id) === root.selectionAnchorId })
                const to = visibleResults.findIndex(function(row) { return Number(row.id) === id })
                if (from >= 0 && to >= 0) {
                    if (modifiers & Qt.ControlModifier) next = Object.assign({}, selectedRows)
                    for (let i = Math.min(from, to); i <= Math.max(from, to); ++i) next[visibleResults[i].id] = true
                } else next[id] = true
            } else {
                if (next[id] === true) delete next[id]; else next[id] = true
                selectionAnchorId = id
            }
            exportSelectionChanged(next)
        }
        resultSelected(id)
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

    onSourceChoicesChanged: if (sourceFilter && sourceChoices.indexOf(sourceFilter) < 0) sourceFilter = ""
    onSourceQueryChanged: { reviewNotice = ""; rebuild() }
    onSourceFilterChanged: { reviewNotice = ""; rebuild() }
    onSelectedIndexChanged: revealSelection()
    onResultsChanged: rebuild()
    onSortDescendingChanged: rebuild()
    onSortCriterionChanged: rebuild()
    onReviewFilterChanged: { rebuild(); if (selectedIndex >= 0 && currentVisibleIndex < 0) resultSelected(visibleResults.length ? Number(visibleResults[0].id) : -1) }
    Connections { target: Analysis; function onResultsChanged() { root.revealedCards = ({}) } }
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
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: controlsColumn; width: parent.width; spacing: 8
        RowLayout {
            Layout.fillWidth: true; spacing: 3; visible: root.results.length > 0
            Repeater {
                model: ["all", "favorites", "hidden"]
                delegate: PfButton {
                    required property string modelData
                    Layout.fillWidth: true; compact: true; selected: root.reviewFilter === modelData
                    text: Review.t(modelData); onClicked: root.reviewFilter = modelData
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.results.length > 0
            spacing: 6
            PfButton { objectName: "selectAllResultsButton"; Layout.fillWidth: true; quiet: true; enabled: root.selectedCount < root.visibleResults.length; text: L10n.t("results.selectAll"); onClicked: root.selectAll() }
            PfButton { objectName: "clearResultsSelectionButton"; Layout.fillWidth: true; quiet: true; enabled: root.selectedCount > 0; text: L10n.t("results.clearSelection"); onClicked: root.clearSelection() }
        }
        PfButton {
            objectName: "clearResultsColorsButton"; Layout.fillWidth: true; quiet: true; compact: true
            visible: root.results.length > 0
            enabled: root.coloredSelectionIds.length > 0 && !Analysis.busy && !Analysis.exportBusy && !Analysis.reviewBusy
            text: L10n.language === "ru" ? "Снять цвет" : "Clear color"
            ToolTip.visible: hovered
            ToolTip.text: root.selectedCount > 0
                ? (L10n.language === "ru" ? "Снять цвет с выбранных пар" : "Clear color from selected pairs")
                : (L10n.language === "ru" ? "Снять цвет со всех видимых пар" : "Clear color from all visible pairs")
            onClicked: root.clearColors()
        }

        TextField {
            id: sourceSearch; objectName: "resultSourceSearch"; Layout.fillWidth: true; visible: root.results.length > 0
            placeholderText: Review.t("sourceSearch"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11
            Accessible.name: Review.t("sourceSearch")
            background: Rectangle { radius: Theme.radiusButton; color: Theme.well; border.color: sourceSearch.activeFocus ? Theme.accent : Theme.hairlineStrong }
            onTextChanged: root.sourceQuery = text
            ToolTip.visible: hovered; ToolTip.text: Review.t("sourceHint")
        }
        PfComboBox {
            objectName: "resultSourceFilter"; Layout.fillWidth: true; visible: root.results.length > 0
            model: root.sourceChoices.map(function(path) { return path ? root.sourceName(path) : Review.t("allSources") })
            currentIndex: Math.max(0, root.sourceChoices.indexOf(root.sourceFilter))
            Accessible.name: Review.t("allSources")
            onActivated: function(index) { root.sourceFilter = root.sourceChoices[index] || "" }
            ToolTip.visible: hovered && !!root.sourceFilter; ToolTip.text: root.sourceFilter
        }
        RowLayout {
            Layout.fillWidth: true; visible: root.results.length > 0; spacing: 6
            PfButton { objectName: "nextUnreviewedButton"; Layout.fillWidth: true; compact: true; text: Review.t("nextUnreviewed"); onClicked: root.selectNextUnreviewed() }
            PfButton { objectName: "undoReviewButton"; compact: true; text: Review.t("undo"); enabled: Analysis.canUndoReview && !Analysis.busy && !Analysis.reviewBusy && !Analysis.exportBusy; onClicked: root.undoRequested() }
        }
        Text { Layout.fillWidth: true; visible: !!root.reviewNotice; text: root.reviewNotice; color: Theme.textSecondary; wrapMode: Text.WordWrap; font.family: Theme.fontFamily; font.pixelSize: 10 }
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
                text: root.results.length > 0 ? root.visibleResults.length + " " + L10n.t("results.found") + " · " + root.visibleResults.filter(function(row) { return !row.reviewed }).length + " " + Review.t("unreviewed").toLowerCase() : L10n.t("results.emptyHint")
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
        PfButton {
            Layout.fillWidth: true
            visible: root.results.length > 0
            text: L10n.t("results.export") + (root.selectedCount > 0 ? " · " + root.selectedCount : "")
            enabled: root.selectedCount > 0 && root.reviewFilter !== "hidden"
            primary: enabled
            onClicked: root.exportRequested(root.exportRows())
        }
        PfButton { Layout.fillWidth: true; compact: true; quiet: true; visible: root.visibleResults.length > 0 && root.reviewFilter !== "hidden"; text: Review.t("sheet"); onClicked: root.findingsRequested() }

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
            model: displayModel
            add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionChangeDuration } }
            remove: Transition {
                SequentialAnimation {
                    PropertyAction { property: "ListView.delayRemove"; value: true }
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionChangeDuration }
                        NumberAnimation { property: "entranceOffset"; to: 10; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
                    }
                    PropertyAction { property: "ListView.delayRemove"; value: false }
                }
            }
            displaced: Transition { NumberAnimation { properties: "x,y"; duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic } }
            focus: true
            activeFocusOnTab: true
            Accessible.name: L10n.t("results.title")
            Keys.onReturnPressed: if (root.visibleResults.length > 0) root.resultSelected(root.selectedIndex < 0 ? Number(root.visibleResults[0].id) : root.selectedIndex)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: resultCard
                objectName: "resultCard" + Number(modelData.id)
                z: Number(modelData.id) === root.selectedIndex ? 2 : 0
                onHeightChanged: Qt.callLater(root.layoutResults)
                property var modelData: model.entry
                enabled: !ListView.delayRemove
                ListView.onRemove: cardReveal.stop()
                property real entranceOffset: 0
                property real reflowOffset: 0
                transform: Translate { y: resultCard.entranceOffset + resultCard.reflowOffset }
                Component.onCompleted: {
                    // Only six first-visible cards, never thousands of rows.
                    if (index < 6 && !Theme.reducedMotion) {
                        if (!root.revealedCards[modelData.id]) {
                            root.revealedCards[modelData.id] = true
                            opacity = 0; entranceOffset = 6; cardReveal.start()
                        }
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
                height: Math.max(120, Math.ceil(resultTextMetrics.height) * 6 + 36)
                    + (modelData && modelData.category ? Math.ceil(resultTextMetrics.height) + 4 : 0)
                radius: 7
                color: root.selectedRows[modelData.id] ? Theme.accentMuted : Theme.surfaceRaised
                Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                border.color: Number(modelData.id) === root.selectedIndex ? Theme.accent : Theme.border
                Behavior on border.color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                Rectangle {
                    x: 0; y: 14; width: 2; height: parent.height - 28; radius: 1
                    color: modelData.categoryColor || Theme.accent
                    Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionChangeDuration } }
                    opacity: modelData.category ? 1 : Number(modelData.id) === root.selectedIndex ? 1 : 0
                    Behavior on opacity { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration } }
                }
                Accessible.name: L10n.matchLabel(modelData) + " " + Math.round(root.movementSimilarity(modelData) * 100) + "%"
                Accessible.role: Accessible.ListItem

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: function(mouse) { resultList.forceActiveFocus(); root.selectCard(Number(modelData.id), mouse.modifiers) }
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

                    Button {
                        id: pairColorButton
                        objectName: "resultColorButton" + modelData.id
                        width: 18; height: 28
                        enabled: !Analysis.busy
                        Accessible.name: L10n.t("results.categoryColor") + " " + (Number(modelData.id) + 1)
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        background: Rectangle {
                            anchors.centerIn: parent; width: 18; height: 18; radius: 4
                            color: modelData.categoryColor || Theme.well
                            Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionChangeDuration } }
                            border.color: pairColorButton.hovered ? Theme.accent : Theme.hairlineStrong
                        }
                        onClicked: {
                            root.colorTargetId = Number(modelData.id)
                            const point = mapToItem(Overlay.overlay, 0, height)
                            colorPicker.x = Math.max(8, Math.min(point.x, Overlay.overlay.width - colorPicker.width - 8))
                            colorPicker.y = Math.max(8, Math.min(point.y, Overlay.overlay.height - colorPicker.height - 8))
                            colorPicker.open()
                        }
                    }
                    Column {
                        id: rowContent
                        width: Math.max(0, parent.width - 84)
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
                            objectName: "resultColorLabel" + modelData.id
                            text: "●  " + L10n.colorLabel(modelData.categoryColor); color: modelData.categoryColor || Theme.accent; elide: Text.ElideRight
                            Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionChangeDuration } }
                        }
                        Repeater {
                            model: ["left", "right"]
                            delegate: Text {
                                required property string modelData
                                width: rowContent.width; font.family: Theme.fontFamily; font.pixelSize: 10
                                property string source: String(resultCard.modelData[modelData + "Source"] || "")
                                text: (modelData === "left" ? "A · " : "B · ") + root.sourceName(source)
                                color: (root.sourceFilter || root.sourceQuery.trim()) && root.matchesSource(source) ? Theme.accent : Theme.textSecondary; elide: Text.ElideMiddle
                                Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                                font.weight: (root.sourceFilter || root.sourceQuery.trim()) && root.matchesSource(source) ? Font.DemiBold : Font.Normal
                                ToolTip.visible: sourceHover.containsMouse
                                ToolTip.text: source
                                MouseArea { id: sourceHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                            }
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
                    Column {
                        width: 24; spacing: 5
                        PfIconButton {
                            objectName: "removePairButton" + modelData.id
                            width: 24; height: 26; iconSize: 14
                            iconSource: modelData.hidden ? "qrc:/qt/qml/PfUi/qml/assets/chevron-left.svg" : "qrc:/qt/qml/PfUi/qml/assets/x.svg"
                            accessibleName: Review.t(modelData.hidden ? "restore" : "remove")
                            enabled: !Analysis.busy && !Analysis.exportBusy && !Analysis.reviewBusy
                            onClicked: root.pairReviewRequested(Number(modelData.id), "hidden", !modelData.hidden)
                        }
                        PfButton {
                            objectName: "favoritePairButton" + modelData.id
                            width: 24; height: 26; implicitHeight: 26; compact: true; quiet: true
                            text: modelData.favorite ? "★" : "☆"; selected: !!modelData.favorite
                            Accessible.name: Review.t("favorite"); ToolTip.visible: hovered; ToolTip.text: Accessible.name
                            enabled: !Analysis.busy && !Analysis.exportBusy && !Analysis.reviewBusy
                            onClicked: root.pairReviewRequested(Number(modelData.id), "favorite", !modelData.favorite)
                        }
                        PfButton {
                            objectName: "reviewedPairButton" + modelData.id
                            width: 24; height: 24; implicitHeight: 24; compact: true; quiet: true
                            text: "✓"; selected: !!modelData.reviewed
                            Accessible.name: Review.t(modelData.reviewed ? "reviewed" : "unreviewed")
                            ToolTip.visible: hovered; ToolTip.text: modelData.reviewed ? Review.t("reviewed") : Review.t("reviewedHint")
                            enabled: !Analysis.busy && !Analysis.reviewBusy && !Analysis.exportBusy
                            onClicked: root.pairReviewRequested(Number(modelData.id), "reviewed", !modelData.reviewed)
                        }
                    }
                }
            }

            Text {
                font.family: Theme.fontFamily
                anchors.centerIn: parent
                visible: root.visibleResults.length === 0
                width: parent.width - 28
                text: root.results.length === 0 ? L10n.t("results.emptyBody") : Review.t("empty")
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
