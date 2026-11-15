import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import PfUi
import PfUiBridge

Item {
    id: root
    objectName: "targetCharacterPanel"
    property string chosenId: ""
    property string inspectedId: ""
    property bool showUnresolved: false
    readonly property int unresolvedCount: Analysis.characterCandidates.filter(function(row) { return row.unresolved }).length
    readonly property int confirmedCount: Analysis.characterCandidates.length - unresolvedCount
    readonly property var visibleCandidates: Analysis.characterCandidates.filter(function(row) { return root.showUnresolved || root.confirmedCount === 0 || !row.unresolved })
    function sourceName(path) { return String(path).replace(/\\/g,"/").split("/").pop() }
    function inspectPhoto(appearance) {
        if (!appearance || !appearance.url) return
        photo.source = appearance.url
        photo.personBox = appearance.box || null
        photoDialog.closeUp = true
        photoDialog.brightened = false
        photoDialog.open()
    }
    Popup {
        id: photoDialog
        property bool closeUp: true
        property bool brightened: false
        objectName: "characterPhotoDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(960, parent.width - 32)
        height: Math.min(680, parent.height - 32)
        modal: true
        focus: true
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.panelAlt; radius: Theme.radiusButton; border.color: Theme.border }
        enter: Transition {
            enabled: !Theme.reducedMotion
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 210; easing.type: Easing.OutQuint }
        }
        exit: Transition {
            enabled: !Theme.reducedMotion
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110; easing.type: Easing.InCubic }
        }
        onClosed: photo.source = ""
        contentItem: ColumnLayout {
            Item {
                id: viewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                CharacterAppearance {
                    id: photo
                    width: viewport.width
                    height: viewport.height
                    transformOrigin: Item.TopLeft
                    readonly property real targetScale: photoDialog.closeUp && marked
                        ? Math.max(1, Math.min(6, width / Math.max(1, (personRight-personLeft)*1.28),
                                                  height / Math.max(1, (personBottom-personTop)*1.28))) : 1
                    scale: targetScale
                    layer.enabled: photoDialog.brightened
                    layer.effect: MultiEffect { brightness: 0.22 }
                    x: targetScale > 1 ? width/2 - targetScale*(personLeft+personRight)/2 : 0
                    y: targetScale > 1 ? height/2 - targetScale*(personTop+personBottom)/2 : 0
                    Behavior on scale { enabled: !Theme.reducedMotion; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    Behavior on x { enabled: !Theme.reducedMotion; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    Behavior on y { enabled: !Theme.reducedMotion; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                PfButton {
                    objectName: "toggleCharacterPhotoFraming"
                    visible: photo.validBox
                    text: L10n.t(photoDialog.closeUp ? "character.fullFrame" : "character.closeUp")
                    onClicked: photoDialog.closeUp = !photoDialog.closeUp
                }
                PfButton {
                    objectName: "toggleCharacterPhotoBrightness"
                    text: L10n.t(photoDialog.brightened ? "character.originalBrightness" : "character.brightenPhoto")
                    selected: photoDialog.brightened
                    onClicked: photoDialog.brightened = !photoDialog.brightened
                }
                Item { Layout.fillWidth: true }
                PfButton {
                    text: L10n.t("character.closePhoto")
                    onClicked: photoDialog.close()
                }
            }
        }
    }
    Connections {
        target: Analysis
        function onTargetSelectionChanged() { photoDialog.close(); root.chosenId=""; root.inspectedId=""; root.showUnresolved=false }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        Text { Layout.fillWidth: true; text: L10n.t("character.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 22; font.weight: Font.DemiBold }
        Text { Layout.fillWidth: true; text: root.sourceName(Analysis.targetSelectionSource); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; elide: Text.ElideMiddle }
        Text { Layout.fillWidth: true; text: L10n.t("character.hint"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
        ListView {
            id: candidates
            objectName: "targetCharacterList"
            Layout.fillWidth: true; Layout.fillHeight: true
            model: root.visibleCandidates
            spacing: 8; clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Accessible.name: L10n.t("character.title")
            activeFocusOnTab: true
            displaced: Transition { enabled: !Theme.reducedMotion; NumberAnimation { properties: "x,y"; duration: Theme.motionChangeDuration; easing.type: Easing.InOutCubic } }
            delegate: Rectangle {
                id: candidate
                required property var modelData
                readonly property var representative: modelData.representative || (modelData.previews.length ? modelData.previews[0] : null)
                width: ListView.view.width
                height: 112 + (root.inspectedId === modelData.id ? 158 : 0)
                radius: Theme.radiusButton
                border.color: root.chosenId === modelData.id ? Theme.accent : Theme.border
                color: root.chosenId === modelData.id ? Theme.accentMuted : Theme.panelAlt
                Behavior on height { enabled: !Theme.reducedMotion; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                clip: true
                RowLayout {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                    height: 88; spacing: 12
                    CharacterAppearance {
                        Layout.preferredWidth: Math.min(156,candidate.width*.28); Layout.preferredHeight: 88
                        source: candidate.representative ? candidate.representative.url : ""
                        personBox: candidate.representative ? candidate.representative.box : null
                        Button {
                            objectName: "openCharacterPhoto"
                            anchors.fill: parent
                            enabled: !!candidate.representative && !!candidate.representative.url
                            Accessible.name: L10n.t("character.openPhoto")
                            ToolTip.visible: hovered
                            ToolTip.text: Accessible.name
                            background: Rectangle { color: "transparent"; border.color: parent.visualFocus ? Theme.accent : "transparent"; radius: 4 }
                            onClicked: root.inspectPhoto(candidate.representative)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 6
                        Text { Layout.fillWidth: true; text: L10n.t(candidate.modelData.unresolved ? "character.unresolvedAppearance" : "character.number").arg(String(candidate.modelData.number).padStart(2,"0")); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; text: L10n.t("character.presence").arg(Math.round(candidate.modelData.presence)).arg(candidate.modelData.tracks); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        PfButton { compact: true; quiet: true; text: L10n.t("character.scenes"); onClicked: root.inspectedId = root.inspectedId === candidate.modelData.id ? "" : candidate.modelData.id }
                    }
                    PfButton {
                        Layout.preferredWidth: Math.min(110,candidate.width*.22); compact: true
                        selected: root.chosenId === candidate.modelData.id; text: L10n.t("character.select")
                        Accessible.name: L10n.t("character.select") + " " + L10n.t("character.number").arg(candidate.modelData.number)
                        onClicked: root.chosenId=candidate.modelData.id
                    }
                }
                ListView {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12
                    height: 146; visible: root.inspectedId === candidate.modelData.id
                    orientation: ListView.Horizontal; spacing: 8; clip: true
                    model: candidate.modelData.previews
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Column {
                        required property var modelData
                        width: 200; spacing: 6
                        CharacterAppearance {
                            width: 200; height: 112; source: modelData.url; personBox: modelData.box || null
                            Button {
                                anchors.fill: parent
                                enabled: !!modelData.url
                                Accessible.name: L10n.t("character.openPhoto")
                                ToolTip.visible: hovered
                                ToolTip.text: Accessible.name
                                background: Rectangle { color: "transparent"; border.color: parent.visualFocus ? Theme.accent : "transparent"; radius: 4 }
                                onClicked: root.inspectPhoto(modelData)
                            }
                        }
                        Text { text: L10n.t("character.time").arg(Number(modelData.time).toFixed(1)); color: Theme.textSecondary; font.family: Theme.monoFont; font.pixelSize: 10 }
                    }
                }
            }
        }
        PfButton {
            objectName: "unresolvedAppearancesButton"
            visible: root.unresolvedCount>0 && root.confirmedCount>0; compact: true; quiet: true
            text: root.showUnresolved ? L10n.t("character.hideUnresolved") : L10n.t("character.showUnresolved").arg(root.unresolvedCount)
            onClicked: root.showUnresolved=!root.showUnresolved
        }
        Text {
            objectName: "unresolvedLimitNotice"
            Layout.fillWidth: true
            visible: Analysis.hiddenUnresolvedCharacters > 0 && (root.showUnresolved || root.confirmedCount === 0)
            text: L10n.t("character.unresolvedLimit").arg(root.unresolvedCount).arg(root.unresolvedCount + Analysis.hiddenUnresolvedCharacters)
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
        PfButton {
            objectName: "confirmCharacterButton"; Layout.alignment: Qt.AlignRight; Layout.preferredWidth: Math.min(260,root.width)
            primary: true; text: L10n.t("character.continue"); enabled: root.chosenId.length > 0
            onClicked: Analysis.selectTargetCharacter(root.chosenId)
        }
        PfButton {
            objectName: "analyzeAllPeopleButton"
            Layout.fillWidth: true; quiet: true
            text: L10n.t("character.allPeople")
            enabled: Analysis.awaitingTarget
            onClicked: Analysis.continueWithoutTargetCharacter()
        }
    }
}
