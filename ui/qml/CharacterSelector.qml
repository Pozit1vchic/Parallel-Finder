import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi
import PfUiBridge

Item {
    id: root
    objectName: "targetCharacterPanel"
    property string chosenId: ""
    property string inspectedId: ""
    function sourceName(path) { return String(path).replace(/\\/g,"/").split("/").pop() }
    Connections {
        target: Analysis
        function onTargetSelectionChanged() { root.chosenId=""; root.inspectedId="" }
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
            model: Analysis.characterCandidates
            spacing: 8; clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Accessible.name: L10n.t("character.title")
            activeFocusOnTab: true
            displaced: Transition { enabled: !Theme.reducedMotion; NumberAnimation { properties: "x,y"; duration: Theme.motionChangeDuration; easing.type: Easing.InOutCubic } }
            delegate: Rectangle {
                id: candidate
                required property var modelData
                width: ListView.view.width
                height: 112 + (root.inspectedId === modelData.id ? 158 : 0)
                radius: Theme.radiusButton
                border.color: root.chosenId === modelData.id ? Theme.accent : Theme.border
                color: root.chosenId === modelData.id ? Theme.accentMuted : Theme.panelAlt
                Behavior on height { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.InOutCubic } }
                Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
                clip: true
                RowLayout {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                    height: 88; spacing: 12
                    Image {
                        Layout.preferredWidth: Math.min(156,candidate.width*.28); Layout.preferredHeight: 88
                        source: candidate.modelData.previews.length ? candidate.modelData.previews[0].url : ""
                        fillMode: Image.PreserveAspectFit; asynchronous: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 6
                        Text { Layout.fillWidth: true; text: L10n.t("character.number").arg(String(candidate.modelData.number).padStart(2,"0")); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 14; font.weight: Font.DemiBold }
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
                        Image { width: 200; height: 112; source: modelData.url; fillMode: Image.PreserveAspectFit; asynchronous: true }
                        Text { text: L10n.t("character.time").arg(Number(modelData.time).toFixed(1)); color: Theme.textSecondary; font.family: Theme.monoFont; font.pixelSize: 10 }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
        PfButton {
            objectName: "confirmCharacterButton"; Layout.alignment: Qt.AlignRight; Layout.preferredWidth: Math.min(260,root.width)
            primary: true; text: L10n.t("character.continue"); enabled: root.chosenId.length > 0
            onClicked: Analysis.selectTargetCharacter(root.chosenId)
        }
    }
}
