import QtQuick
import QtQuick.Controls.Basic
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
        const summary = String(AppInfo.gpuSummary || "Auto")
        return summary.split("·")[0].trim() || "Auto"
    }
    function backendStatus(id) {
        const key = String(id || "auto").toLowerCase()
        if (key === "auto") return "Готов к работе"
        return AppInfo.backendAvailable(key) ? "Готов к работе" : "Недоступен"
    }

    Rectangle { anchors.fill: parent; color: Theme.canvas }
    Row {
        anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 12; height: 40
        Text { text: L10n.t("app.title"); color: "#F4F4F5"; font.family: Theme.fontFamily; font.pixelSize: 15; font.weight: Font.DemiBold; font.letterSpacing: -0.15; anchors.verticalCenter: parent.verticalCenter }
        Rectangle { width: 1; height: 16; color: "#27272A"; anchors.verticalCenter: parent.verticalCenter }
        Text { font.family: Theme.fontFamily; text: L10n.t("top.workspace"); color: "#A1A1AA"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
    }
    RowLayout {
        anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
        Text { font.family: Theme.fontFamily; Layout.alignment: Qt.AlignVCenter; text: root.busy ? L10n.t("top.analyzing") : L10n.t("top.ready"); color: root.busy ? Theme.accent : Theme.textSecondary; font.pixelSize: 11 }
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 226
            Layout.minimumWidth: 168
            Layout.maximumWidth: 250
            width: 226; height: 28; radius: 8
            color: AppInfo.backendIsGpu ? Theme.sageMuted : Theme.surfaceRaised; border.color: Theme.border
            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                Rectangle { Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: Analysis.providerChoice === "auto" || AppInfo.backendAvailable(Analysis.providerChoice) ? "#22C55E" : Theme.textDisabled
                    SequentialAnimation on opacity { running: AppInfo.backendIsGpu && !Theme.reducedMotion && root.Window.active; loops: Animation.Infinite; NumberAnimation { to: 0.5; duration: 1100 } NumberAnimation { to: 1; duration: 1100 } }
                }
                Text { font.family: Theme.fontFamily;
                    id: gpuLabel
                    Layout.fillWidth: true
                    text: Analysis.providerChoice === "auto"
                        ? backendLabel("auto") + " · " + backendStatus("auto")
                        : backendLabel(Analysis.providerChoice) + " · " + backendStatus(Analysis.providerChoice)
                    color: Analysis.providerChoice !== "auto" && !AppInfo.backendAvailable(Analysis.providerChoice)
                        ? Theme.accent : "#E4E4E7"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
            ToolTip.visible: gpuHover.hovered
            ToolTip.delay: 400
            ToolTip.text: Analysis.providerChoice === "auto"
                ? AppInfo.gpuSummary
                : backendLabel(Analysis.providerChoice) + " · "
                  + (AppInfo.backendAvailable(Analysis.providerChoice)
                     ? (AppInfo.gpuDevice || "готов к работе")
                     : (AppInfo.backendReason(Analysis.providerChoice) || "runtime не найден"))
            HoverHandler { id: gpuHover }
        }
        PfIconButton { Layout.alignment: Qt.AlignVCenter; iconSource: "qrc:/qt/qml/PfUi/qml/assets/settings.svg"; accessibleName: L10n.t("top.settings"); onClicked: root.settingsRequested() }
    }
}
