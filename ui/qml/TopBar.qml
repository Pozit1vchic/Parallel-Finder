import QtQuick
import QtQuick.Layouts
import PfUi
import PfUiBridge

Item {
    id: root
    property bool busy: false
    signal settingsRequested()
    implicitHeight: Theme.topBarHeight

    function backendLabel(id) {
        const key = String(id || "auto").toLowerCase()
        if (key === "dml") return "DirectML"
        if (key === "cuda") return "CUDA"
        if (key === "tensorrt") return "TensorRT"
        if (key === "cpu") return "CPU"
        return AppInfo.gpuSummary
    }

    Rectangle { anchors.fill: parent; color: Theme.canvas }
    Row {
        anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
        Text { text: L10n.t("app.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 22 }
        Rectangle { width: 1; height: 22; color: Theme.hairline; anchors.verticalCenter: parent.verticalCenter }
        Text { text: L10n.t("top.workspace"); color: Theme.textSecondary; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
    }
    RowLayout {
        anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
        Text { Layout.alignment: Qt.AlignVCenter; text: root.busy ? L10n.t("top.analyzing") : L10n.t("top.ready"); color: root.busy ? Theme.accent : Theme.textSecondary; font.pixelSize: 11 }
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: gpuLabel.implicitWidth + 30; width: implicitWidth; height: 28; radius: 14
            color: AppInfo.backendIsGpu ? Theme.sageMuted : Theme.surfaceRaised; border.color: Theme.border
            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 7
                Rectangle { Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 5; Layout.preferredHeight: 5; radius: 3; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textDisabled }
                Text {
                    id: gpuLabel
                    Layout.fillWidth: true
                    text: Analysis.providerChoice === "auto"
                        ? AppInfo.gpuSummary
                        : backendLabel(Analysis.providerChoice) + " · "
                          + (AppInfo.backendAvailable(Analysis.providerChoice)
                             ? (AppInfo.gpuDevice || "готов") : "недоступен")
                    color: Analysis.providerChoice !== "auto" && !AppInfo.backendAvailable(Analysis.providerChoice)
                        ? Theme.accent : (AppInfo.backendIsGpu ? Theme.sageBright : Theme.textSecondary)
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
        PfButton { Layout.alignment: Qt.AlignVCenter; text: L10n.t("top.settings"); quiet: true; onClicked: root.settingsRequested() }
    }
}
