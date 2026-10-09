import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi
import PfUiBridge

Flickable {
    id: root
    objectName: "settingsHotkeysFlick"
    clip: true
    contentWidth: width
    contentHeight: body.implicitHeight + 24
    boundsBehavior: Flickable.StopAtBounds
    property string errorText: ""
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    ColumnLayout {
        id: body; width: parent.width; spacing: 12
        Text { text: Review.t("hotkeys"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
        Text { Layout.fillWidth: true; text: Review.t("keysHint"); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
        PfCheckBox { objectName: "multiSelectCheck"; text: Review.t("multi"); checked: Review.multiSelect; onToggled: Review.setMulti(checked) }
        Text { Layout.fillWidth: true; text: Review.t("multiHint"); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
        Text { visible: !!root.errorText; Layout.fillWidth: true; text: root.errorText; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: body.width; spacing: 4
            readonly property real keyColumnWidth: Math.max(116, width * 0.28)
            readonly property real actionColumnWidth: Math.max(0, width - keyColumnWidth * 2 - 16)
            id: keyTable
            RowLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 8; Layout.preferredHeight: 24
                Text { Layout.minimumWidth: keyTable.actionColumnWidth; Layout.maximumWidth: keyTable.actionColumnWidth; Layout.preferredWidth: keyTable.actionColumnWidth; text: L10n.language === "ru" ? "Действие" : "Action"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
                Repeater {
                    model: 2
                    delegate: Text {
                        required property int index
                        Layout.minimumWidth: keyTable.keyColumnWidth; Layout.maximumWidth: keyTable.keyColumnWidth; Layout.preferredWidth: keyTable.keyColumnWidth
                        text: (L10n.language === "ru" ? "Клавиша " : "Key ") + (index + 1)
                        color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
            Repeater {
                model: Review.actions
                delegate: RowLayout {
                    id: actionRow
                    required property var modelData
                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.minimumHeight: 28; spacing: 8
                    Text { Layout.minimumWidth: keyTable.actionColumnWidth; Layout.maximumWidth: keyTable.actionColumnWidth; Layout.preferredWidth: keyTable.actionColumnWidth; text: Review.label(actionRow.modelData); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                    Repeater {
                        model: 2
                        delegate: RowLayout {
                            required property int index
                            Layout.minimumWidth: keyTable.keyColumnWidth; Layout.maximumWidth: keyTable.keyColumnWidth; Layout.preferredWidth: keyTable.keyColumnWidth
                            spacing: 3
                            PfButton {
                                id: capture
                                objectName: "hotkey_" + actionRow.modelData.id + "_" + index
                                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 28
                                implicitHeight: 28; height: 28; compact: true
                                topPadding: 2; bottomPadding: 2
                                quiet: true; selected: capturing
                                property bool capturing: false
                                text: capturing ? Review.t("press") : (Review.keys(actionRow.modelData.id)[index] || Review.t("unset"))
                                contentItem: Text {
                                    text: capture.text; font.family: Theme.fontFamily; font.pixelSize: 11
                                    color: capture.capturing ? Theme.accent : Review.keys(actionRow.modelData.id)[index] ? Theme.textPrimary : Theme.textDisabled
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight; maximumLineCount: 1
                                }
                                onClicked: { root.errorText = ""; capturing = true; forceActiveFocus() }
                                onActiveFocusChanged: if (!activeFocus) capturing = false
                                Keys.onPressed: function(event) {
                                    if (!capturing) return
                                    event.accepted = true
                                    if (event.key === Qt.Key_Escape) { capturing = false; return }
                                    const sequence = AppInfo.keySequence(event.key, event.modifiers)
                                    if (!sequence) return
                                    root.errorText = Review.assign(actionRow.modelData.id, index, sequence)
                                    if (!root.errorText) capturing = false
                                }
                            }
                            PfIconButton {
                                objectName: "clear_hotkey_" + actionRow.modelData.id + "_" + index
                                iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"; iconSize: 12
                                Layout.minimumWidth: 22; Layout.maximumWidth: 22; Layout.preferredWidth: 22; Layout.preferredHeight: 26
                                opacity: Review.keys(actionRow.modelData.id)[index] ? 1 : 0
                                enabled: opacity > 0
                                accessibleName: Review.t("clearKey")
                                onClicked: { Review.assign(actionRow.modelData.id, index, ""); root.errorText = "" }
                            }
                        }
                    }
                }
            }
        }
        PfButton { text: Review.t("reset"); compact: true; implicitHeight: 30; height: 30; quiet: true; onClicked: { Review.reset(); root.errorText = "" } }
    }
}
