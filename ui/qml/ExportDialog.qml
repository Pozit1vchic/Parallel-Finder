import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import PfUi
import PfUiBridge

Popup {
    id: root
    property Item rootWindow
    property var selectedRows: ({})
    property string outputFolder: ""
    property string exportStatus: ""
    modal: true; focus: true; padding: 0
    width: Math.min(600, rootWindow ? rootWindow.width - 40 : 560); height: 520
    x: rootWindow ? Math.round((rootWindow.width - width) / 2) : 0; y: rootWindow ? Math.round((rootWindow.height - height) / 2) : 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.70) }
    background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairline; layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.78); shadowBlur: 1.0; shadowVerticalOffset: 18 } }
    FolderDialog { id: folderDialog; title: L10n.t("export.chooseFolder"); onAccepted: root.outputFolder = selectedFolder.toString().replace(/^file:\/\//, "") }
    Connections { target: Analysis; function onExportFinished(success, message) { root.exportStatus = message; if (success) root.close() } }
    function selectedIndexes() { return Object.keys(root.selectedRows).map(function (key) { return Number(key) }) }
    contentItem: Column { anchors.fill: parent; anchors.margins: 24; spacing: 14
        Row { width: parent.width; height: 38
            Column { width: parent.width - 42; spacing: 4
                Text { text: L10n.t("export.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 25 }
                Text { text: Object.keys(root.selectedRows).length + " " + L10n.t("export.selected"); color: Theme.textSecondary; font.pixelSize: 12 }
            }
            PfIconButton { iconSource: "qrc:/qt/qml/PfUi/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: root.close() }
            MouseArea {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.rightMargin: 42
                property real pressX
                property real pressY
                onPressed: { pressX = mouse.x; pressY = mouse.y }
                onPositionChanged: if (pressed && rootWindow) {
                    root.x = Math.max(12, Math.min(rootWindow.width - root.width - 12, root.x + mouse.x - pressX))
                    root.y = Math.max(12, Math.min(rootWindow.height - root.height - 12, root.y + mouse.y - pressY))
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
        Text { text: L10n.t("export.format"); color: Theme.textSecondary; font.pixelSize: 11 }
        ComboBox { id: exportFormat; width: parent.width; height: 36; model: ["JSON", "CSV", "TXT", "EDL", "FCPXML", "AEP"] }
        Text { text: L10n.t("export.numbering"); color: Theme.textSecondary; font.pixelSize: 11 }
            Row { width: parent.width; spacing: 8
                PfButton { width: (parent.width - 8) / 2; text: L10n.t("export.asVideo"); quiet: exportNumbering.currentIndex !== 0; onClicked: exportNumbering.currentIndex = 0 }
                PfButton { width: (parent.width - 8) / 2; text: L10n.t("export.bySort"); quiet: exportNumbering.currentIndex !== 1; onClicked: exportNumbering.currentIndex = 1 }
            }
        ComboBox { id: exportNumbering; visible: false; model: [0, 1]; currentIndex: 0 }
        Text { text: L10n.t("export.cutMode"); color: Theme.textSecondary; font.pixelSize: 11 }
        Row { width: parent.width; spacing: 8
            PfButton { width: (parent.width - 8) / 2; text: L10n.t("export.exact"); quiet: cutMode.currentIndex !== 0; onClicked: cutMode.currentIndex = 0 }
            PfButton { width: (parent.width - 8) / 2; text: L10n.t("export.fast"); quiet: cutMode.currentIndex !== 1; onClicked: cutMode.currentIndex = 1 }
        }
        ComboBox { id: cutMode; visible: false; model: [0, 1]; currentIndex: 0 }
        Row { width: parent.width; spacing: 8
            TextField { id: folderField; width: parent.width - 110; text: root.outputFolder; placeholderText: L10n.t("export.folder"); onEditingFinished: root.outputFolder = text }
            PfButton { width: 102; text: L10n.t("export.chooseFolder"); quiet: true; onClicked: folderDialog.open() }
        }
        TextField { id: prefixField; width: parent.width; text: "frame_"; placeholderText: L10n.t("export.prefix") }
        Text { width: parent.width; text: root.exportStatus || L10n.t("export.hint"); color: root.exportStatus ? Theme.accent : Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap }
        PfButton { width: parent.width; text: L10n.t("export.prepare"); sageAction: true; enabled: Object.keys(root.selectedRows).length > 0 && root.outputFolder.length > 0; onClicked: Analysis.exportResults(exportFormat.currentText, exportNumbering.currentIndex, cutMode.currentIndex, root.outputFolder, prefixField.text, root.selectedIndexes()) }
    }
}
