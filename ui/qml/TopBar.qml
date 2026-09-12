import QtQuick
import PfUi
import PfUiBridge

Item {
    id: root
    property bool busy: false
    signal settingsRequested()
    implicitHeight: Theme.topBarHeight

    Rectangle { anchors.fill: parent; color: Theme.canvas }
    Row {
        anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
        Text { text: L10n.t("app.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 22 }
        Rectangle { width: 1; height: 22; color: Theme.hairline; anchors.verticalCenter: parent.verticalCenter }
        Text { text: L10n.t("top.workspace"); color: Theme.textSecondary; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
        Text { text: root.busy ? L10n.t("top.analyzing") : L10n.t("top.ready"); color: root.busy ? Theme.accent : Theme.textSecondary; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
        Rectangle {
            width: gpuLabel.implicitWidth + 22; height: 28; radius: 14
            color: AppInfo.backendIsGpu ? Theme.sageMuted : Theme.surfaceRaised; border.color: Theme.border
            Row { anchors.centerIn: parent; spacing: 7
                Rectangle { width: 5; height: 5; radius: 3; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textDisabled; anchors.verticalCenter: parent.verticalCenter }
                Text { id: gpuLabel; text: AppInfo.gpuSummary.toUpperCase(); color: AppInfo.backendIsGpu ? Theme.sageBright : Theme.textSecondary; font.pixelSize: 10 }
            }
        }
        PfButton { text: L10n.t("top.settings"); quiet: true; onClicked: root.settingsRequested() }
    }
}
