import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi
import PfUiBridge

ReviewDialog {
    id: root
    objectName: "manualSortDialog"
    title: russian ? "Сортировка вручную" : "Manual sorting"
    width: Math.min(1080, rootWindow ? rootWindow.width - 32 : 1000)
    height: Math.min(780, rootWindow ? rootWindow.height - 32 : 720)
    property var rows: []
    property var selectedIds: []
    property int anchorIndex: -1
    property int anchorId: -1
    property int page: 0
    readonly property int pageSize: 12
    readonly property int pageCount: Math.max(1, Math.ceil(rows.length / pageSize))
    readonly property var pageRows: rows.slice(page * pageSize, (page + 1) * pageSize)
    readonly property bool russian: L10n.language === "ru"
    readonly property bool canEdit: !Analysis.busy && !Analysis.exportBusy && !Analysis.reviewBusy
    readonly property var groupNames: {
        const names = []
        rows.forEach(function(row) { const name = String(row.manualGroup || ""); if (name && names.indexOf(name) < 0) names.push(name) })
        return names
    }
    property string errorText: ""
    property int assignmentRevision: 0
    property real pageOffset: 0
    signal findingsRequested()

    function pick(index, modifiers) {
        if (index < 0 || index >= rows.length) return
        const id = rows[index].id
        let next = selectedIds.slice()
        if ((modifiers & Qt.ShiftModifier) && anchorIndex >= 0) {
            if (!(modifiers & Qt.ControlModifier)) next = []
            const first = Math.min(index, anchorIndex), last = Math.max(index, anchorIndex)
            for (let n = first; n <= last; ++n) if (next.indexOf(rows[n].id) < 0) next.push(rows[n].id)
        } else if (modifiers & Qt.ControlModifier) {
            const position = next.indexOf(id)
            if (position < 0) next.push(id); else next.splice(position, 1)
            anchorIndex = index
            anchorId = id
        } else {
            next = [id]
            anchorIndex = index
            anchorId = id
        }
        selectedIds = next
        errorText = ""
    }
    function assign(name) {
        const clean = String(name).trim()
        if (!selectedIds.length || !canEdit) return
        if (Analysis.setResultGroup(selectedIds, clean)) {
            rows = rows.map(function(row) { return selectedIds.indexOf(row.id) >= 0 ? Object.assign({}, row, {manualGroup: clean}) : row })
            ++assignmentRevision
            errorText = ""
        } else errorText = russian ? "Не удалось изменить группу. Обнови список результатов и повтори." : "Cannot change the group. Refresh the results and try again."
    }
    function goToPage(next) {
        const destination = Math.max(0, Math.min(pageCount - 1, next))
        if (destination === page) return
        pageMotion.stop()
        pageOffset = Theme.reducedMotion ? 0 : (destination > page ? 22 : -22)
        page = destination
        scroller.contentY = 0
        if (!Theme.reducedMotion) pageMotion.start()
    }
    onRowsChanged: {
        anchorIndex = rows.findIndex(function(row) { return row.id === anchorId })
        if (page >= pageCount) page = pageCount - 1
        selectedIds = selectedIds.filter(function(id) { return rows.some(function(row) { return row.id === id }) })
    }
    onAboutToShow: { errorText = ""; selectedIds = []; anchorIndex = -1; anchorId = -1; page = 0 }
    onAboutToHide: { pageMotion.stop(); pageOffset = 0 }
    Connections { target: Analysis; function onResultsChanged() { if (root.visible) root.close(); root.rows = []; root.selectedIds = []; root.anchorIndex = -1; root.anchorId = -1 } }
    Connections { target: Theme; function onReducedMotionChanged() { pageMotion.stop(); root.pageOffset = 0 } }
    NumberAnimation { id: pageMotion; target: root; property: "pageOffset"; to: 0; duration: 220; easing.type: Easing.OutCubic }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Text {
            Layout.fillWidth: true
            text: root.russian ? "Собери свои группы из пар и одиночных моментов. Классификация движения сохранится." : "Build your own groups from pairs and single moments. Motion classifications stay intact."
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 13; wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            TextField {
                id: groupName
                objectName: "manualGroupName"
                Layout.fillWidth: true; implicitHeight: 36
                placeholderText: root.russian ? "Название своей группы" : "Your group name"
                maximumLength: 80
                color: Theme.textPrimary; placeholderTextColor: Theme.textDisabled
                font.family: Theme.fontFamily; font.pixelSize: 13
                background: Rectangle { color: Theme.well; radius: Theme.radiusButton; border.color: groupName.activeFocus ? Theme.accent : Theme.hairline }
                onAccepted: if (text.trim()) root.assign(text)
            }
            PfButton { objectName: "manualGroupAssign"; compact: true; text: root.russian ? "В группу" : "Assign group"; enabled: root.canEdit && root.selectedIds.length > 0 && !!groupName.text.trim(); onClicked: root.assign(groupName.text) }
            PfButton { objectName: "manualGroupClear"; compact: true; text: root.russian ? "Убрать группу" : "Clear group"; enabled: root.canEdit && root.selectedIds.length > 0; onClicked: root.assign("") }
        }
        RowLayout {
            Layout.fillWidth: true
            PfComboBox {
                id: existingGroups
                objectName: "manualExistingGroups"
                Layout.fillWidth: true
                model: [root.russian ? "Выбрать существующую группу…" : "Choose an existing group…"].concat(root.groupNames)
                onActivated: if (currentIndex > 0) groupName.text = root.groupNames[currentIndex - 1]
            }
            PfButton { compact: true; text: root.russian ? "Выбрать страницу" : "Select page"; enabled: root.pageRows.length > 0; onClicked: root.selectedIds = root.pageRows.map(function(row) { return row.id }) }
            PfButton { compact: true; text: root.russian ? "Снять выбор" : "Clear selection"; enabled: root.selectedIds.length > 0; onClicked: { root.selectedIds = []; root.anchorIndex = -1; root.anchorId = -1 } }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: (root.russian ? "Выбрано: " : "Selected: ") + root.selectedIds.length + (root.russian ? " · Ctrl — добавить, Shift — диапазон" : " · Ctrl to add, Shift for a range"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }
        Flickable {
            id: scroller
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 80
            clip: true; contentWidth: width; contentHeight: cards.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            GridLayout {
                id: cards
                x: root.pageOffset
                width: scroller.width - 12
                columns: width >= 760 ? 3 : 2
                columnSpacing: 10; rowSpacing: 10
                Repeater {
                    model: root.pageRows
                    delegate: Rectangle {
                        id: card
                        objectName: "manualSortCard" + modelData.id
                        required property var modelData
                        required property int index
                        readonly property bool picked: root.selectedIds.indexOf(modelData.id) >= 0
                        Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 170
                        color: picked ? Theme.accentMuted : Theme.well
                        radius: Theme.radiusCard; border.width: picked || activeFocus ? 2 : 1
                        border.color: picked || activeFocus ? Theme.accent : Theme.hairline
                        activeFocusOnTab: true
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: (root.russian ? "Результат " : "Result ") + (Number(modelData.id) + 1)
                        Accessible.checked: picked
                        Keys.onSpacePressed: function(event) { root.pick(root.page * root.pageSize + index, event.modifiers | Qt.ControlModifier); event.accepted = true }
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 9; spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: (card.picked ? "✓  " : "") + "№ " + (Number(card.modelData.id) + 1); color: card.picked ? Theme.accent : Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; font.bold: true }
                                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: L10n.classificationLabel(card.modelData.classification); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; elide: Text.ElideRight }
                            }
                            RowLayout {
                                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
                                Repeater {
                                    model: ["left", "right"]
                                    delegate: Rectangle {
                                        required property string modelData
                                        readonly property bool removed: !!card.modelData[modelData + "Removed"]
                                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 1
                                        color: Theme.canvas; radius: 4; clip: true
                                        Image { anchors.fill: parent; anchors.margins: 2; fillMode: Image.PreserveAspectFit; visible: !parent.removed; source: parent.removed ? "" : card.modelData[parent.modelData + "Preview"] || ""; asynchronous: true; sourceSize.width: 480 }
                                        Text { anchors.centerIn: parent; visible: parent.removed; text: root.russian ? "Удалено" : "Removed"; color: Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 10 }
                                        Text { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 4; text: modelData === "left" ? "A" : "B"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 10 }
                                    }
                                }
                            }
                            Text {
                                id: groupStamp
                                Layout.fillWidth: true
                                text: card.modelData.manualGroup || (root.russian ? "Без своей группы" : "No custom group")
                                color: card.modelData.manualGroup ? Theme.accent : Theme.textDisabled
                                font.family: Theme.fontFamily; font.pixelSize: 12; elide: Text.ElideRight
                                transformOrigin: Item.Left
                                SequentialAnimation {
                                    id: assignedMotion
                                    NumberAnimation { target: groupStamp; property: "scale"; to: 0.94; duration: 70; easing.type: Easing.InQuad }
                                    NumberAnimation { target: groupStamp; property: "scale"; to: 1; duration: 180; easing.type: Easing.OutCubic }
                                }
                                Connections { target: root; function onAssignmentRevisionChanged() { if (card.picked && !Theme.reducedMotion) assignedMotion.restart() } function onAboutToHide() { assignedMotion.stop(); groupStamp.scale = 1 } }
                                Connections { target: Theme; function onReducedMotionChanged() { assignedMotion.stop(); groupStamp.scale = 1 } }
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: function(mouse) { card.forceActiveFocus(); root.pick(root.page * root.pageSize + card.index, mouse.modifiers) } }
                    }
                }
            }
            Text { anchors.centerIn: parent; visible: root.rows.length === 0; text: root.russian ? "Здесь появятся результаты анализа." : "Analysis results will appear here."; color: Theme.textSecondary; font.family: Theme.fontFamily }
        }
        Text { Layout.fillWidth: true; visible: !!root.errorText; text: root.errorText; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            PfButton { objectName: "manualSortPrevious"; compact: true; text: root.russian ? "Назад" : "Previous"; enabled: root.page > 0; onClicked: root.goToPage(root.page - 1) }
            Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: (root.page + 1) + " / " + root.pageCount; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12 }
            PfButton { objectName: "manualSortNext"; compact: true; text: root.russian ? "Далее" : "Next"; enabled: root.page + 1 < root.pageCount; onClicked: root.goToPage(root.page + 1) }
            PfButton { compact: true; text: root.russian ? "Лист находок" : "Contact sheet"; enabled: root.rows.length > 0; onClicked: root.findingsRequested() }
        }
    }
}
