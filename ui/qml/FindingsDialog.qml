import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import PfUi
import PfUiBridge

ReviewDialog {
    id: root
    objectName: "findingsDialog"
    title: Review.t("sheet")
    height: Math.min(470, rootWindow ? rootWindow.height - 32 : 470)
    property var rows: []
    property var markedRows: ({})
    property string outputFolder: ""
    property string statusText: ""
    property bool awaiting: false
    function indexes() { return rows.filter(function(row) { return !row.hidden && (scope.currentIndex === 0 || markedRows[row.id] === true) }).map(function(row) { return Number(row.id) }) }
    FolderDialog { id: folderDialog; title: Review.t("folder"); onAccepted: root.outputFolder = String(selectedFolder) }
    Connections {
        target: Analysis
        function onExportFinished(success, message) { if (root.awaiting) { root.statusText = message; root.awaiting = false } }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        Text { Layout.fillWidth: true; text: Review.t("sheetHint"); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
        Text { text: Review.t("scope"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11 }
        PfComboBox { id: scope; Layout.fillWidth: true; model: [Review.t("visible"), Review.t("marked")]; enabled: !Analysis.exportBusy }
        Text { text: Review.t("frames"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11 }
        PfComboBox { id: frameCount; Layout.fillWidth: true; model: ["1", "3", "5"]; currentIndex: 1; enabled: !Analysis.exportBusy }
        PfButton { Layout.fillWidth: true; text: root.outputFolder ? root.outputFolder : Review.t("choose"); quiet: true; enabled: !Analysis.exportBusy; onClicked: folderDialog.open() }
        Text { Layout.fillWidth: true; text: root.statusText; color: Theme.textSecondary; wrapMode: Text.WordWrap; font.family: Theme.fontFamily; font.pixelSize: 11 }
        Item { Layout.fillHeight: true }
        PfButton { Layout.fillWidth: true; primary: true; text: Analysis.exportBusy ? Review.t("saving") + " " + Analysis.exportCompleted + "/" + Analysis.exportTotal : Review.t("save") + " · " + root.indexes().length; enabled: !Analysis.busy && !Analysis.reviewBusy && !Analysis.exportBusy && root.outputFolder.length > 0 && root.indexes().length > 0; onClicked: { root.awaiting = true; root.statusText = ""; if (!Analysis.exportFindings(root.outputFolder, root.indexes(), [1, 3, 5][frameCount.currentIndex])) { root.awaiting = false; root.statusText = Review.t("invalid") } } }
        PfButton { Layout.fillWidth: true; visible: Analysis.exportBusy && root.awaiting; text: L10n.t("export.cancel"); onClicked: Analysis.cancelExport() }
    }
}
