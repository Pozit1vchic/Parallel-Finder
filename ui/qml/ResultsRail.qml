import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi
import PfUiBridge
import "OrientationEvidence.js" as Orientation

Rectangle {
    id: root
    objectName: "resultsRail"
    property var results: []
    property var selectedRows: ({})
    property bool sortDescending: false
    property int selectedIndex: -1
    property string sortCriterion: "classification"
    signal exportSelectionChanged(var rows)
    signal resultSelected(int index)
    signal pairOpened(int index)
    signal exportRequested(var rows)
    signal pairClassRequested(int index, string classification)
    property string classFilter: ""
    property string scopeFilter: ""
    property string postureFilter: ""
    property string bodyViewFilter: ""
    property string headViewFilter: ""
    readonly property var orientationChoices: ["", "uncertain", "1", "2", "3", "4", "5", "6", "7", "8"]
    function orientationLabel(choice) {
        return L10n.t(!choice ? "filter.orientation.all" : choice === "uncertain" ? "orientation.unknown" : "orientation.view" + choice)
    }
    readonly property var postureChoices: ["", "standing", "sitting", "lying", "crouching", "kneeling", "uncertain"]
    function matchesPosture(descriptor) {
        const posture=descriptor ? Number(descriptor.posture || 0) : 0
        const evidence=descriptor ? Number(descriptor.postureEvidence || 0) : 0
        if(postureFilter === "uncertain") return posture === 0 || posture === 6 || evidence < 2
        return evidence >= 2 && posture === postureChoices.indexOf(postureFilter)
    }
    readonly property var classKeys: L10n.movementKeysForRows(results)
    onClassKeysChanged: rebuild()
    property var classificationCounts: ({})
    property var movementGroupCounts: ({})
    property int bulkThreshold: 120
    property bool useStagingModel: false
    property bool bulkTargetStaging: true
    property bool bulkUpdating: false
    property bool bulkTransitionsSuppressed: false
    property bool suppressFilterRebuild: false
    property int rebuildSerial: 0
    property var pendingRows: []
    property int pendingRowIndex: 0
    property real pendingContentY: 0
    readonly property int activeFilterCount: (classFilter ? 1 : 0) + (sourceQuery.trim() ? 1 : 0)
        + (matchTierFilter !== "strong" ? 1 : 0) + (sourceFilter ? 1 : 0) + (scopeFilter ? 1 : 0) + (postureFilter ? 1 : 0)
        + (sortCriterion !== "classification" || sortDescending ? 1 : 0)
        + (bodyViewFilter ? 1 : 0) + (headViewFilter ? 1 : 0)
    function classKey(row) {
        return L10n.movementKey(row.classification)
    }
    property var classChoices: [""]
    signal pairReviewRequested(int index, string field, bool value)
    signal findingsRequested()
    signal undoRequested()
    property string sourceQuery: ""
    property string sourceFilter: ""
    property string reviewNotice: ""
    readonly property var sourceChoices: {
        const sources = [""]
        const seen = new Set()
        for (const row of results) for (const side of ["left", "right"]) {
            if (row[side + "Removed"]) continue
            const source = String(row[side + "Source"] || "")
            if (source && !seen.has(source)) { seen.add(source); sources.push(source) }
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
                openPair(Number(visibleResults[index].id))
                return
            }
        }
        reviewNotice = Review.t("allReviewed")
    }
    property string matchTierFilter: "strong"
    property string reviewFilter: "all"
    property int selectionAnchorId: -1
    property var revealedCards: ({})
    ListModel { id: displayModel }
    ListModel { id: stagingModel }
    Timer {
        id: sourceSearchDebounce
        interval: 140
        repeat: false
        onTriggered: { root.sourceQuery = sourceSearch.text }
    }
    Timer {
        id: largeModelTimer
        interval: 1
        repeat: true
        onTriggered: root.appendLargeModelChunk()
    }
    Timer { id: bulkMotionRelease; interval: 260; repeat: false; onTriggered: root.bulkTransitionsSuppressed = false }
    property int classTargetId: -1
    Popup {
        id: classPicker
        objectName: "resultClassPicker"
        parent: Overlay.overlay
        property Item anchorItem: null
        function placeAtAnchor() {
            if (!anchorItem || !parent) return
            const point = anchorItem.mapToItem(parent, 0, anchorItem.height)
            x = Math.max(8, Math.min(point.x, parent.width - width - 8))
            y = Math.max(8, Math.min(point.y, parent.height - height - 8))
        }
        onOpened: placeAtAnchor()
        padding: 8
        width: 226
        height: Math.min(440, L10n.movementTypes.length * 30 + 16,
                         parent ? Math.max(120, parent.height - 32) : 440)
        transformOrigin: Item.TopLeft
        enter: Transition {
            enabled: !Theme.reducedMotion
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: Theme.motionPopupScale; to: 1; duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic }
                NumberAnimation { property: "y"; from: classPicker.y - Theme.motionPopupOffset; to: classPicker.y; duration: Theme.motionPopupDuration; easing.type: Easing.OutQuint }
            }
        }
        exit: Transition {
            enabled: !Theme.reducedMotion
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale"; to: 0.985; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
                NumberAnimation { property: "y"; to: classPicker.y - 4; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
            }
        }
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusButton; border.color: Theme.hairlineStrong }
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            contentHeight: classChoicesColumn.implicitHeight
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            Column {
                id: classChoicesColumn
                spacing: 2
                Repeater {
                    model: L10n.movementTypes
                    delegate: PfButton {
                        required property int index
                        required property string modelData
                        objectName: "resultClassChoice_" + modelData
                        width: 210; height: 28; compact: true; quiet: true
                        text: L10n.classificationLabel(modelData)
                        onClicked: { root.pairClassRequested(root.classTargetId, modelData); classPicker.close() }
                    }
                }
            }
        }
    }
    Popup {
        id: selectionMenu
        objectName: "resultSelectionMenu"
        parent: Overlay.overlay
        property Item anchorItem: null
        function placeAtAnchor() {
            if (!anchorItem || !parent) return
            const point = anchorItem.mapToItem(parent, 0, anchorItem.height)
            x = Math.max(8, Math.min(point.x, parent.width - width - 8))
            y = Math.max(8, Math.min(point.y, parent.height - height - 8))
        }
        onOpened: placeAtAnchor()
        width: 244
        padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairlineStrong }
        transformOrigin: Item.TopLeft
        enter: Transition {
            enabled: !Theme.reducedMotion
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: Theme.motionPopupScale; to: 1; duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic }
                NumberAnimation { property: "y"; from: selectionMenu.y - Theme.motionPopupOffset; to: selectionMenu.y; duration: Theme.motionPopupDuration; easing.type: Easing.OutQuint }
            }
        }
        exit: Transition {
            enabled: !Theme.reducedMotion
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale"; to: 0.985; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
                NumberAnimation { property: "y"; to: selectionMenu.y - 4; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
            }
        }
        contentItem: ColumnLayout {
            spacing: 6
            Text { Layout.fillWidth: true; Layout.bottomMargin: 2; text: Review.t("selection"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold }
            PfButton {
                objectName: "selectAllResultsButton"
                Layout.fillWidth: true
                compact: true
                quiet: true
                enabled: root.selectedCount < root.visibleResults.length
                text: L10n.t("results.selectAll") + " · " + root.visibleResults.length
                onClicked: { root.selectAll(); selectionMenu.close() }
            }
            PfButton {
                objectName: "clearResultsSelectionButton"
                Layout.fillWidth: true
                compact: true
                quiet: true
                enabled: root.selectedCount > 0
                text: L10n.t("results.clearSelection")
                onClicked: { root.clearSelection(); selectionMenu.close() }
            }
            Text { Layout.fillWidth: true; text: Review.t("selectionHint"); color: Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
    }
    Popup {
        id: filterMenu
        objectName: "resultFiltersMenu"
        parent: Overlay.overlay
        property Item anchorItem: null
        function placeAtAnchor() {
            if (!anchorItem || !parent) return
            const point = anchorItem.mapToItem(parent, 0, anchorItem.height)
            x = Math.max(8, Math.min(point.x, parent.width - width - 8))
            y = Math.max(8, Math.min(point.y, parent.height - height - 8))
        }
        onOpened: placeAtAnchor()
        width: 300
        height: Math.min(430, parent ? Math.max(180, parent.height - 24) : 430)
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairlineStrong }
        transformOrigin: Item.TopLeft
        enter: Transition {
            enabled: !Theme.reducedMotion
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: Theme.motionPopupScale; to: 1; duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic }
                NumberAnimation { property: "y"; from: filterMenu.y - Theme.motionPopupOffset; to: filterMenu.y; duration: Theme.motionPopupDuration; easing.type: Easing.OutQuint }
            }
        }
        exit: Transition {
            enabled: !Theme.reducedMotion
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale"; to: 0.985; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
                NumberAnimation { property: "y"; to: filterMenu.y - 4; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
            }
        }
        contentItem: ScrollView {
            objectName: "resultFiltersScroll"
            clip: true
            contentWidth: availableWidth
            contentHeight: filterMenuColumn.implicitHeight
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ColumnLayout {
                id: filterMenuColumn
                width: parent.width
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: L10n.t("results.filtersAndOrder"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 14; font.weight: Font.DemiBold }
                    PfButton {
                        objectName: "resetResultFiltersButton"
                        compact: true; quiet: true
                        text: L10n.t("results.resetFilters")
                        enabled: root.activeFilterCount > 0
                        onClicked: root.resetFilters()
                    }
                }
                Text { Layout.fillWidth: true; text: L10n.t("ui.349"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10 }
                PfComboBox {
                    objectName: "resultClassFilterCombo"; Layout.fillWidth: true
                    Accessible.name: L10n.t("ui.349")
                    model: root.classChoices.map(function(key) { return key ? L10n.classificationLabel(key) + " · " + Number(root.classificationCounts[key] || 0) : (L10n.t("results.allClasses")) })
                    selectionIndex: root.classChoices.indexOf(root.classFilter)
                    onActivated: function(index) { root.classFilter = root.classChoices[index] }
                }
                TextField {
                    id: sourceSearch; objectName: "resultSourceSearch"; Layout.fillWidth: true
                    placeholderText: Review.t("sourceSearch"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11
                    Accessible.name: Review.t("sourceSearch")
                    background: Rectangle { radius: Theme.radiusButton; color: Theme.well; border.color: sourceSearch.activeFocus ? Theme.accent : Theme.hairlineStrong }
                    onTextChanged: sourceSearchDebounce.restart()
                    onAccepted: { sourceSearchDebounce.stop(); root.sourceQuery = text }
                    ToolTip.visible: hovered; ToolTip.text: Review.t("sourceHint")
                }
                PfComboBox {
                    objectName: "resultSourceFilter"; Layout.fillWidth: true
                    model: root.sourceChoices.map(function(path) { return path ? root.sourceName(path) : Review.t("allSources") })
                    selectionIndex: Math.max(0, root.sourceChoices.indexOf(root.sourceFilter))
                    Accessible.name: Review.t("allSources")
                    onActivated: function(index) { root.sourceFilter = root.sourceChoices[index] || "" }
                    ToolTip.visible: hovered && !!root.sourceFilter; ToolTip.text: root.sourceFilter
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline; Layout.topMargin: 2; Layout.bottomMargin: 2 }
                PfComboBox {
                    objectName: "matchTierFilter"; Layout.fillWidth: true
                    model: [L10n.t("matches.strong")+" · "+root.results.filter(function(row) { return !row.hidden && (row.matchTier || "strong") === "strong" }).length,
                        L10n.t("matches.possible")+" · "+root.results.filter(function(row) { return !row.hidden && row.matchTier === "possible" }).length]
                    selectionIndex: root.matchTierFilter === "possible" ? 1 : 0
                    onActivated: function(index) { root.matchTierFilter=index === 1 ? "possible" : "strong" }
                    Accessible.name: L10n.t("matches.tier")
                }
                PfComboBox {
                    objectName: "resultScopeFilter"; Layout.fillWidth: true
                    model: [L10n.t("filter.scopeAll"),L10n.t("filter.scopeHead"),L10n.t("filter.scopeBody")]
                    selectionIndex: Math.max(0,["","head","body"].indexOf(root.scopeFilter))
                    onActivated: function(index) { root.scopeFilter=["","head","body"][index] }
                    Accessible.name: L10n.t("filter.scopeAll")
                }
                PfComboBox {
                    objectName: "resultPostureFilter"; Layout.fillWidth: true
                    model: root.postureChoices.map(function(key) { return L10n.t("filter.posture."+(key || "all")) })
                    selectionIndex: Math.max(0,root.postureChoices.indexOf(root.postureFilter))
                    onActivated: function(index) { root.postureFilter=root.postureChoices[index] }
                    Accessible.name: L10n.t("filter.posture.all")
                }
                Text { Layout.fillWidth: true; text: L10n.t("orientation.title"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10 }
                PfComboBox {
                    objectName: "resultBodyViewFilter"; Layout.fillWidth: true
                    model: root.orientationChoices.map(root.orientationLabel)
                    selectionIndex: Math.max(0,root.orientationChoices.indexOf(root.bodyViewFilter))
                    onActivated: function(index) { root.bodyViewFilter=root.orientationChoices[index] }
                    Accessible.name: L10n.t("orientation.title")
                }
                Text { Layout.fillWidth: true; text: L10n.t("filter.headView.title"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10 }
                PfComboBox {
                    objectName: "resultHeadViewFilter"; Layout.fillWidth: true
                    model: root.orientationChoices.map(root.orientationLabel)
                    selectionIndex: Math.max(0,root.orientationChoices.indexOf(root.headViewFilter))
                    onActivated: function(index) { root.headViewFilter=root.orientationChoices[index] }
                    Accessible.name: L10n.t("filter.headView.title")
                }
                Text { Layout.fillWidth: true; text: L10n.t("filter.orientation.pair"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
                Text { Layout.fillWidth: true; text: L10n.t("results.sortBy"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10 }
                PfComboBox {
                    id: resultSort
                    objectName: "resultSortCombo"
                    Keys.onPressed: function(event) {
                        if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) < 0) return
                        if (popup.visible) popup.close()
                        resultList.forceActiveFocus()
                        if (event.key === Qt.Key_Up) root.selectPrevious()
                        if (event.key === Qt.Key_Down) root.selectNext()
                        event.accepted = true
                    }
                    Layout.fillWidth: true
                    Accessible.name: L10n.t("results.sortBy")
                    model: [L10n.t("results.scoreHigh"), L10n.t("results.scoreLow"), L10n.t("results.sceneHigh"), L10n.t("results.sceneLow"),
                            L10n.t("results.timeEarly"), L10n.t("results.timeLate"), L10n.t("ui.350"),
                            L10n.t("sort.best"),L10n.t("sort.continuity"),L10n.t("sort.pose"),L10n.t("sort.identity"),
                            L10n.t("sort.evidence"),L10n.t("sort.longest"),L10n.t("sort.shortest"),L10n.t("sort.source"),
                            L10n.t("sort.bodyView"),L10n.t("sort.headView")]
                    selectionIndex: ["best","continuity","pose","identity","evidence","longest","shortest","source","bodyView","headView"].indexOf(root.sortCriterion)>=0
                        ? 7+["best","continuity","pose","identity","evidence","longest","shortest","source","bodyView","headView"].indexOf(root.sortCriterion)
                        : root.sortCriterion === "classification" ? 6 : root.sortCriterion === "time" ? (root.sortDescending ? 5 : 4)
                        : root.sortCriterion === "scene" ? (root.sortDescending ? 2 : 3) : (root.sortDescending ? 0 : 1)
                    onActivated: function(index) { root.chooseSort(index) }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline; Layout.topMargin: 2; Layout.bottomMargin: 2 }
                Text {
                    Layout.fillWidth: true
                    text: root.activeFilterCount > 0
                        ? (L10n.t("ui.351")) + root.activeFilterCount
                        : (L10n.t("ui.352"))
                    color: Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 10
                    wrapMode: Text.WordWrap
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
        root.openPair(Number(visibleResults[0].id))
        resultList.positionViewAtBeginning()
    }

    function openPair(id) {
        const target = Number(id)
        if (target < 0 || !visibleResults.some(function(row) { return Number(row.id) === target })) return
        root.resultSelected(target)
        root.pairOpened(target)
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

    function resetFilters() {
        sourceSearchDebounce.stop()
        suppressFilterRebuild = true
        sourceSearch.text = ""
        sourceSearchDebounce.stop()
        sourceQuery = ""
        sourceFilter = ""
        classFilter = ""
        scopeFilter = ""
        postureFilter = ""
        bodyViewFilter = ""
        headViewFilter = ""
        matchTierFilter = "strong"
        sortCriterion = "classification"
        sortDescending = false
        suppressFilterRebuild = false
        reviewNotice = ""
        rebuild()
    }

    function chooseSort(index) {
        suppressFilterRebuild = true
        sortCriterion = index>=7 ? ["best","continuity","pose","identity","evidence","longest","shortest","source","bodyView","headView"][index-7]
            : index === 6 ? "classification" : index >= 4 ? "time" : index >= 2 ? "scene" : "movement"
        sortDescending = index < 4 ? index % 2 === 0 : index === 5
        suppressFilterRebuild = false
        rebuild()
    }

    function sceneSimilarity(item) {
        return Number(item.sceneSimilarity !== undefined ? item.sceneSimilarity : 0)
    }

    function timeValue(item) {
        return Number(item.leftStart || 0)
    }

    function matchedDuration(item) {
        function sideDuration(side) {
            if (item[side + "Removed"]) return null
            const start = item[side + "Start"], end = item[side + "End"]
            if (!Number.isFinite(start) || !Number.isFinite(end) || end < start) return undefined
            return end - start
        }
        const left = sideDuration("left"), right = sideDuration("right")
        if (left === undefined || right === undefined || (left === null && right === null)) return undefined
        if (left === null) return right
        if (right === null) return left
        return Math.min(left, right)
    }

    function compareValues(a, b) {
        let delta = 0
        const strongA=Number(a.bestMatchScore !== undefined ? a.bestMatchScore : root.movementSimilarity(a))
        const strongB=Number(b.bestMatchScore !== undefined ? b.bestMatchScore : root.movementSimilarity(b))
        if (sortCriterion === "bodyView" || sortCriterion === "headView") {
            const scope=sortCriterion === "bodyView" ? "body" : "head"
            const ka=Orientation.sortKey(a,scope), kb=Orientation.sortKey(b,scope)
            return ka[0]-kb[0] || ka[1]-kb[1] || ka[2]-kb[2] || timeValue(a)-timeValue(b) || Number(a.id)-Number(b.id)
        }
        if (sortCriterion === "best") return (strongB-Number(b.redundancyPenalty || 0))-(strongA-Number(a.redundancyPenalty || 0)) || Number(a.id)-Number(b.id)
        if (sortCriterion === "continuity") {
            const rankedA=Number.isFinite(a.continuityRank) && a.continuityRank >= 0
            const rankedB=Number.isFinite(b.continuityRank) && b.continuityRank >= 0
            if (rankedA !== rankedB) return rankedA ? -1 : 1
            return (rankedA ? a.continuityRank-b.continuityRank : 0) || timeValue(a)-timeValue(b) || Number(a.id)-Number(b.id)
        }
        if (sortCriterion === "pose") {
            const knownA=Number.isFinite(a.poseSimilarity) && a.poseSimilarity >= 0
            const knownB=Number.isFinite(b.poseSimilarity) && b.poseSimilarity >= 0
            if (knownA !== knownB) return knownA ? -1 : 1
            return (knownA ? b.poseSimilarity-a.poseSimilarity : 0) || strongB-strongA || Number(a.id)-Number(b.id)
        }
        if (sortCriterion === "identity") return Number(b.faceVerified ? 2 : b.identityVerified ? 1 : 0)-Number(a.faceVerified ? 2 : a.identityVerified ? 1 : 0) || strongB-strongA || Number(a.id)-Number(b.id)
        if (sortCriterion === "evidence") {
            const knownA=Number.isFinite(a.evidenceQuality) && a.evidenceQuality>=0
            const knownB=Number.isFinite(b.evidenceQuality) && b.evidenceQuality>=0
            if (knownA !== knownB) return knownA ? -1 : 1
            return (knownA ? b.evidenceQuality-a.evidenceQuality : 0) || strongB-strongA || Number(a.id)-Number(b.id)
        }
        if (sortCriterion === "longest" || sortCriterion === "shortest") {
            const da=matchedDuration(a), db=matchedDuration(b)
            const knownA=Number.isFinite(da), knownB=Number.isFinite(db)
            if (knownA !== knownB) return knownA ? -1 : 1
            if (!knownA) return strongB-strongA || Number(a.id)-Number(b.id)
            return (da-db)*(sortCriterion === "longest" ? -1 : 1) || strongB-strongA || Number(a.id)-Number(b.id)
        }
        if (sortCriterion === "source") return String(a.leftSource || "").localeCompare(String(b.leftSource || "")) || timeValue(a)-timeValue(b) || Number(a.id)-Number(b.id)
        if (sortCriterion === "classification") {
            const ga = String(a.manualGroup || ""), gb = String(b.manualGroup || "")
            if (ga !== gb) return ga && gb ? ga.localeCompare(gb) : ga ? -1 : 1
            delta = classKeys.indexOf(classKey(a)) - classKeys.indexOf(classKey(b))
            if (!delta) {
                // Evidence tiers precede continuity within a semantic group.
                // A high numerical score cannot promote a known conflict.
                function evidenceTier(row) {
                    return row.classificationConflict ? 2 : row.matchTier === "strong" ? 0 : row.matchTier === "possible" ? 1 : 2
                }
                const evidenceDelta=evidenceTier(a)-evidenceTier(b)
                if (evidenceDelta) return evidenceDelta
                // Preserve pair continuity; imported rows without ranks use quality.
                const rankedA=Number.isFinite(a.continuityRank) && a.continuityRank>=0
                const rankedB=Number.isFinite(b.continuityRank) && b.continuityRank>=0
                if (rankedA !== rankedB) return rankedA ? -1 : 1
                if (rankedA && rankedB)
                    return a.continuityRank-b.continuityRank || strongB-strongA || Number(a.id)-Number(b.id)
                return strongB-strongA || Number(a.id)-Number(b.id)
            }
        } else if (sortCriterion === "scene")
            delta = sceneSimilarity(a) - sceneSimilarity(b)
        else if (sortCriterion === "time")
            delta = timeValue(a) - timeValue(b)
        else
            delta = movementSimilarity(a) - movementSimilarity(b)
        if (Math.abs(delta) > 1e-9)
            return delta * (sortDescending ? -1 : 1)
        return (Number(a.id) - Number(b.id))
    }

    function appendLargeModelChunk() {
        const target = bulkTargetStaging ? stagingModel : displayModel
        const end = Math.min(pendingRowIndex + 80, pendingRows.length)
        for (; pendingRowIndex < end; ++pendingRowIndex) {
            const item = pendingRows[pendingRowIndex]
            target.append({entry: item, movementGroup: L10n.exportGroupKey(item)})
        }
        if (pendingRowIndex < pendingRows.length) return
        largeModelTimer.stop()
        const previousActiveWasStaging = useStagingModel
        useStagingModel = bulkTargetStaging
        bulkUpdating = false
        pendingRows = []
        pendingRowIndex = 0
        bulkTransitionsSuppressed = true
        bulkMotionRelease.restart()
        const serial = rebuildSerial
        Qt.callLater(function() {
            if (serial !== root.rebuildSerial) return
            resultList.forceLayout()
            resultList.contentY = Math.max(resultList.originY,
                Math.min(root.pendingContentY, resultList.originY + Math.max(0, resultList.contentHeight - resultList.height)))
            resultList.currentIndex = root.currentVisibleIndex
            // Keep the inactive model empty; it is the staging buffer for the
            // next large sort/filter operation.
            if (previousActiveWasStaging !== root.useStagingModel) {
                if (previousActiveWasStaging) stagingModel.clear()
                else displayModel.clear()
            }
        })
    }
    function rebuild() {
        const previousRows = visibleResults
        const previousIndex = currentVisibleIndex
        const previousId = selectedIndex
        const previousY = resultList.contentY
        const activeModel = useStagingModel ? stagingModel : displayModel
        const nextClassCounts = {}
        for (const item of (results || [])) {
            if (item.hidden) continue
            const key = classKey(item)
            nextClassCounts[key] = Number(nextClassCounts[key] || 0) + 1
        }
        const nextClassChoices = [""]
        for (const key of classKeys) if (Number(nextClassCounts[key] || 0) > 0) nextClassChoices.push(key)
        suppressFilterRebuild = true
        if (classFilter && nextClassChoices.indexOf(classFilter) < 0) classFilter = ""
        classChoices = nextClassChoices
        suppressFilterRebuild = false
        classificationCounts = nextClassCounts
        const next = []
        for (const item of (results || [])) {
            if (reviewFilter === "hidden" ? !item.hidden : item.hidden) continue
            if (reviewFilter === "favorites" && !item.favorite) continue
            if ((item.leftRemoved || !matchesSource(item.leftSource)) && (item.rightRemoved || !matchesSource(item.rightSource))) continue
            if (classFilter && classKey(item) !== classFilter) continue
            if (scopeFilter && item.evidenceScope !== scopeFilter) continue
            if (postureFilter) {
                const postures=Orientation.remainingDescriptors(item).map(root.matchesPosture)
                if(postureFilter === "uncertain" ? !postures.some(Boolean) : !postures.every(Boolean)) continue
            }
            if (!Orientation.matches(item,"body",bodyViewFilter) || !Orientation.matches(item,"head",headViewFilter)) continue
            if (!item.manualPair && (item.matchTier || "strong") !== matchTierFilter) continue
            next.push(item)
        }
        next.sort(compareValues)
        const wanted = {}
        const nextGroupCounts = {}
        for (const item of next) {
            wanted[item.id] = true
            const group = L10n.exportGroupKey(item)
            nextGroupCounts[group] = Number(nextGroupCounts[group] || 0) + 1
        }
        movementGroupCounts = nextGroupCounts
        rebuildSerial += 1
        const serial = rebuildSerial
        const largeUpdate = largeModelTimer.running || Math.max(next.length, activeModel.count) > bulkThreshold
        if (largeUpdate) {
            largeModelTimer.stop()
            bulkUpdating = true
            bulkTransitionsSuppressed = true
            bulkMotionRelease.stop()
            pendingRows = next
            pendingRowIndex = 0
            pendingContentY = previousY
            bulkTargetStaging = !useStagingModel
            const target = bulkTargetStaging ? stagingModel : displayModel
            target.clear()
            visibleResults = next
            if (next.some(function(row) { return !row.reviewed })) reviewNotice = ""
            if (previousId >= 0 && !next.some(function(row) { return Number(row.id) === previousId })) {
                const successor = previousRows.slice(Math.max(0, previousIndex + 1)).find(function(row) { return wanted[row.id] })
                const predecessor = previousRows.slice(0, Math.max(0, previousIndex)).reverse().find(function(row) { return wanted[row.id] })
                const selectedTarget = successor || predecessor || next[0]
                resultSelected(selectedTarget ? Number(selectedTarget.id) : -1)
            }
            if (next.length === 0) appendLargeModelChunk()
            else largeModelTimer.start()
            return
        }
        largeModelTimer.stop()
        const inactiveModel = useStagingModel ? displayModel : stagingModel
        if (inactiveModel.count > 0) inactiveModel.clear()
        bulkUpdating = false
        bulkTransitionsSuppressed = false
        bulkMotionRelease.stop()
        for (let i = activeModel.count - 1; i >= 0; --i)
            if (!wanted[activeModel.get(i).entry.id]) activeModel.remove(i)
        for (let i = 0; i < next.length; ++i) {
            let existing = -1
            for (let j = i; j < activeModel.count; ++j)
                if (Number(activeModel.get(j).entry.id) === Number(next[i].id)) { existing = j; break }
            if (existing < 0) activeModel.insert(i, {entry: next[i], movementGroup: L10n.exportGroupKey(next[i])})
            else {
                if (existing !== i) activeModel.move(existing, i, 1)
                const current = activeModel.get(i)
                if (current.entry !== next[i] || current.movementGroup !== L10n.exportGroupKey(next[i]))
                    activeModel.set(i, {entry: next[i], movementGroup: L10n.exportGroupKey(next[i])})
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
            if (serial !== root.rebuildSerial) return
            resultList.forceLayout()
            resultList.contentY = Math.max(resultList.originY, Math.min(previousY, resultList.originY + Math.max(0, resultList.contentHeight - resultList.height)))
            // A model edit may move the selected result into another group.
            // Keep the reader's viewport; only explicit navigation reveals it.
            resultList.currentIndex = currentVisibleIndex
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
        openPair(id)
    }

    function revealSelection() {
        if (bulkUpdating) return
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
        root.openPair(Number(target.id))
    }

    function selectNext() {
        if (!visibleResults.length) return
        const current = visibleResults.findIndex(function(item) { return Number(item.id) === root.selectedIndex })
        if (current >= visibleResults.length - 1) return
        const target = visibleResults[current < 0 ? 0 : current + 1]
        root.openPair(Number(target.id))
    }

    onSourceChoicesChanged: if (sourceFilter && sourceChoices.indexOf(sourceFilter) < 0) sourceFilter = ""
    onSourceQueryChanged: {
        reviewNotice = ""
        if (sourceSearch.text !== sourceQuery) sourceSearch.text = sourceQuery
        if (!suppressFilterRebuild) rebuild()
    }
    onSourceFilterChanged: { reviewNotice = ""; if (!suppressFilterRebuild) rebuild() }
    onClassFilterChanged: if (!suppressFilterRebuild) rebuild()
    onScopeFilterChanged: if (!suppressFilterRebuild) rebuild()
    onPostureFilterChanged: if (!suppressFilterRebuild) rebuild()
    onBodyViewFilterChanged: if (!suppressFilterRebuild) rebuild()
    onHeadViewFilterChanged: if (!suppressFilterRebuild) rebuild()
    onClassChoicesChanged: if (classFilter && classChoices.indexOf(classFilter) < 0) classFilter = ""
    onSelectedIndexChanged: revealSelection()
    onResultsChanged: rebuild()
    onSortDescendingChanged: if (!suppressFilterRebuild) rebuild()
    onSortCriterionChanged: if (!suppressFilterRebuild) rebuild()
    onMatchTierFilterChanged: if (!suppressFilterRebuild) rebuild()
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
            PfButton {
                objectName: "openResultSelectionButton"
                id: selectionMenuButton
                Layout.fillWidth: true; compact: true; quiet: true
                text: Review.t("selection") + " · " + root.selectedCount
                selected: root.selectedCount > 0
                Accessible.name: Review.t("selection") + ", " + root.selectedCount
                onClicked: { selectionMenu.anchorItem = selectionMenuButton; selectionMenu.open() }
            }
            PfButton {
                objectName: "openResultFiltersButton"
                id: filterMenuButton
                Layout.fillWidth: true; compact: true; quiet: true
                text: L10n.t("results.filtersAndOrder") + (root.activeFilterCount ? " · " + root.activeFilterCount : "")
                selected: root.activeFilterCount > 0
                Accessible.name: text
                onClicked: { filterMenu.anchorItem = filterMenuButton; filterMenu.open() }
            }
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

        PfButton {
            objectName: "viewPossibleMatchesButton"
            Layout.fillWidth: true; compact: true; quiet: true
            visible: root.results.length>0 && root.visibleResults.length===0 && root.matchTierFilter==="strong"
                && root.results.some(function(row) { return !row.hidden && row.matchTier==="possible" })
            text: L10n.t("matches.possible")+" · "+root.results.filter(function(row) { return !row.hidden && row.matchTier==="possible" }).length
            onClicked: root.matchTierFilter="possible"
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

        PfButton {
            Layout.fillWidth: true
            visible: root.results.length > 0
            text: L10n.t("results.export") + (root.selectedCount > 0 ? " · " + root.selectedCount : "")
            enabled: root.selectedCount > 0 && root.reviewFilter !== "hidden"
            primary: enabled
            onClicked: root.exportRequested(root.exportRows())
        }
        PfButton { Layout.fillWidth: true; compact: true; quiet: true; visible: root.visibleResults.length > 0 && root.reviewFilter !== "hidden"; text: L10n.t("ui.353"); onClicked: root.findingsRequested() }

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
            highlightFollowsCurrentItem: false
            clip: true
            spacing: 5
            model: root.useStagingModel ? stagingModel : displayModel
            section.property: root.sortCriterion === "classification" ? "movementGroup" : ""
            section.delegate: Text {
                required property string section
                width: resultList.width; height: 30; verticalAlignment: Text.AlignVCenter
                text: L10n.classificationLabel(section) + " · " + Number(root.movementGroupCounts[section] || 0)
                color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold
            }
            add: Transition { enabled: !Theme.reducedMotion && !root.bulkTransitionsSuppressed && root.visibleResults.length <= root.bulkThreshold; NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionChangeDuration } }
            remove: Transition {
                enabled: !Theme.reducedMotion && !root.bulkTransitionsSuppressed && root.visibleResults.length <= root.bulkThreshold
                SequentialAnimation {
                    PropertyAction { property: "ListView.delayRemove"; value: true }
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionChangeDuration }
                        NumberAnimation { property: "entranceOffset"; to: 10; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
                    }
                    PropertyAction { property: "ListView.delayRemove"; value: false }
                }
            }
            displaced: Transition { enabled: !Theme.reducedMotion && !root.bulkTransitionsSuppressed && root.visibleResults.length <= root.bulkThreshold; NumberAnimation { properties: "x,y"; duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic } }
            focus: true
            activeFocusOnTab: true
            Accessible.name: L10n.t("results.title")
            Keys.onReturnPressed: if (root.visibleResults.length > 0) root.openPair(root.selectedIndex < 0 ? Number(root.visibleResults[0].id) : root.selectedIndex)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: resultCard
                objectName: "resultCard" + Number(modelData.id)
                z: Number(modelData.id) === root.selectedIndex ? 2 : 0
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
                            opacity = 0; entranceOffset = 8; scale = Theme.motionCardScale; cardReveal.start()
                        }
                    }
                }
                SequentialAnimation {
                    id: cardReveal
                    PauseAnimation { duration: Theme.reducedMotion ? 0 : Math.max(0, index) * 20 }
                    ParallelAnimation {
                        NumberAnimation { target: resultCard; property: "opacity"; to: 1; duration: Theme.motionCardDuration; easing.type: Easing.OutCubic }
                        NumberAnimation { target: resultCard; property: "entranceOffset"; to: 0; duration: Theme.motionCardDuration; easing.type: Easing.OutQuint }
                        NumberAnimation { target: resultCard; property: "scale"; to: 1; duration: Theme.motionCardDuration; easing.type: Easing.OutBack; easing.overshoot: 1.05 }
                    }
                }
                Connections {
                    target: Theme
                    function onReducedMotionChanged() {
                        if (!Theme.reducedMotion) return
                        cardReveal.stop(); resultCard.opacity = 1; resultCard.entranceOffset = 0; resultCard.scale = 1
                    }
                }
                width: Math.max(0, resultList.width - 8)
                height: Math.max(120, Math.ceil(rowContent.implicitHeight) + 16)
                radius: 7
                color: root.selectedRows[modelData.id] ? Theme.accentMuted : Theme.surfaceRaised
                Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                border.color: Number(modelData.id) === root.selectedIndex ? Theme.accent : Theme.border
                Behavior on border.color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                Rectangle {
                    x: 0; y: 14; width: 2; height: parent.height - 28; radius: 1
                    color: Theme.accent
                    Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionChangeDuration } }
                    opacity: modelData.category ? 1 : Number(modelData.id) === root.selectedIndex ? 1 : 0
                    Behavior on opacity { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration } }
                }
                Accessible.name: modelData.manualPair ? L10n.matchLabel(modelData) : L10n.matchLabel(modelData) + " " + Math.round(root.movementSimilarity(modelData) * 100) + "%"
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
                        height: 28
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
                        id: pairClassButton
                        objectName: "resultClassButton" + modelData.id
                        width: 34; height: 28
                        padding: 0
                        enabled: !Analysis.busy && !Analysis.exportBusy && !Analysis.reviewBusy
                        Accessible.name: (L10n.t("ui.354")) + (Number(modelData.id) + 1)
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        background: Rectangle {
                            anchors.centerIn: parent; width: 34; height: 24; radius: 7
                            color: pairClassButton.hovered ? Theme.accentMuted : Theme.well
                            Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionChangeDuration } }
                            border.color: pairClassButton.hovered ? Theme.accent : Theme.hairlineStrong
                            Behavior on border.color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionChangeDuration } }
                        }
                        contentItem: Text {
                            objectName: "resultClassButtonLabel" + modelData.id
                            text: L10n.t("ui.355")
                            font.family: Theme.fontFamily; font.pixelSize: 9; font.weight: Font.DemiBold
                            color: pairClassButton.hovered ? Theme.accent : Theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                            Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionChangeDuration } }
                        }
                        onClicked: {
                            root.classTargetId = Number(modelData.id)
                            classPicker.anchorItem = pairClassButton
                            classPicker.open()
                        }
                    }
                    Column {
                        id: rowContent
                        width: Math.max(0, parent.width - 100)
                        spacing: 4

                        Text {
                            font.family: Theme.fontFamily
                            width: parent.width
                            text: modelData.manualPair ? L10n.matchLabel(modelData) : L10n.matchLabel(modelData) + " · " + Math.round(root.movementSimilarity(modelData) * 100) + "%"
                            color: Theme.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            font.family: Theme.fontFamily
                            width: parent.width
                            visible: !modelData.manualPair
                            text: L10n.t("results.sceneSimilarity") + " · " + Math.round(root.sceneSimilarity(modelData) * 100) + "%"
                            color: Theme.secondaryText
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width; font.family: Theme.fontFamily; font.pixelSize: 10
                            text: L10n.classificationLabel(String(modelData.classification || ""))
                            color: Theme.textSecondary; elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width; font.family: Theme.fontFamily; font.pixelSize: 10
                            objectName: "resultManualClassLabel" + modelData.id
                            visible: !!modelData.classificationManual || !!modelData.classificationPartial
                            text: modelData.classificationManual
                                ? (L10n.t("ui.356"))
                                : (L10n.t("ui.357"))
                            color: Theme.textSecondary; elide: Text.ElideRight
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
