import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls
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
        if (AppInfo.backendInitializing) return L10n.t("settings.providerChecking")
        const key = String(id || "auto").toLowerCase()
        return AppInfo.backendAvailable(key) ? L10n.t("top.ready") : L10n.t("settings.providerUnavailable")
    }
    function chooseProvider(id) {
        Analysis.providerChoice = id
        providerMenu.close()
    }

    Rectangle { anchors.fill: parent; color: Theme.canvas }
    Row {
        anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 12; height: 40
        Text { text: L10n.t("app.title"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 15; font.weight: Font.DemiBold; font.letterSpacing: -0.15; anchors.verticalCenter: parent.verticalCenter }
        Rectangle { width: 1; height: 16; color: Theme.hairlineStrong; anchors.verticalCenter: parent.verticalCenter }
        Text { font.family: Theme.fontFamily; text: L10n.t("top.workspace"); color: Theme.textSecondary; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
    }
    RowLayout {
        anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
        Text { font.family: Theme.fontFamily; Layout.alignment: Qt.AlignVCenter; text: root.busy ? L10n.t("top.analyzing") : AppInfo.backendInitializing ? L10n.t("settings.providerChecking") : L10n.t("top.ready"); color: root.busy ? Theme.accent : Theme.textSecondary; font.pixelSize: 11 }
        Rectangle {
            id: providerStatus
            objectName: "topProviderSelector"
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 226
            Layout.minimumWidth: 168
            Layout.maximumWidth: 250
            width: 226; height: 28; radius: 8
            color: providerMenu.visible ? Theme.accentMuted : AppInfo.backendIsGpu ? Theme.sageMuted : Theme.surfaceRaised; border.color: Theme.border
            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                Rectangle { Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: !AppInfo.backendInitializing && AppInfo.backendAvailable(Analysis.providerChoice) ? "#22C55E" : Theme.textDisabled
                }
                Text { font.family: Theme.fontFamily;
                    id: gpuLabel
                    objectName: "gpuStatusLabel"
                    Layout.fillWidth: true
                    text: Analysis.providerChoice === "auto"
                        ? backendLabel("auto") + " · " + backendStatus("auto")
                        : backendLabel(Analysis.providerChoice) + " · " + backendStatus(Analysis.providerChoice)
                    color: !AppInfo.backendInitializing && Analysis.providerChoice !== "auto" && !AppInfo.backendAvailable(Analysis.providerChoice)
                        ? Theme.accent : "#E4E4E7"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
            ToolTip.visible: gpuHover.hovered
            ToolTip.delay: 400
            ToolTip.text: AppInfo.backendInitializing ? L10n.t("settings.providerChecking") : Analysis.providerChoice === "auto"
                ? AppInfo.gpuSummary
                : backendLabel(Analysis.providerChoice) + " · "
                  + (AppInfo.backendAvailable(Analysis.providerChoice)
                     ? (AppInfo.gpuDevice || L10n.t("top.ready"))
                     : (AppInfo.backendReason(Analysis.providerChoice) || L10n.t("settings.providerUnavailable")))
            HoverHandler { id: gpuHover }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: providerMenu.open() }
            Menu {
                id: providerMenu
                y: providerStatus.height + 6
                width: 232
                Repeater {
                    model: ["auto", "cuda", "tensorrt", "dml", "cpu"]
                    delegate: MenuItem {
                        required property string modelData
                        readonly property bool isReady: modelData === "auto" || modelData === "cpu" || AppInfo.backendAvailable(modelData)
                        text: root.backendLabel(modelData) + (isReady ? "" : "  ·  " + L10n.t("settings.providerUnavailable"))
                        enabled: isReady && !Analysis.busy && !Analysis.exportBusy
                        checkable: true
                        checked: Analysis.providerChoice === modelData
                        opacity: isReady ? 1 : 0.45
                        onTriggered: root.chooseProvider(modelData)
                    }
                }
            }
        }
        PfIconButton {
            objectName: "themeToggleButton"
            Layout.alignment: Qt.AlignVCenter
            iconSource: Theme.darkMode ? "qrc:/qt/qml/PfUi/qml/assets/moon.svg" : "qrc:/qt/qml/PfUi/qml/assets/sun.svg"
            accessibleName: L10n.t("top.theme")
            onClicked: {
                Theme.darkMode = !Theme.darkMode
                const prefs = AppInfo.loadPreferences()
                prefs.darkMode = Theme.darkMode
                AppInfo.savePreferences(prefs)
            }
        }
        PfIconButton { objectName: "settingsButton"; Layout.alignment: Qt.AlignVCenter; iconSource: "qrc:/qt/qml/PfUi/qml/assets/settings.svg"; accessibleName: L10n.t("top.settings"); onClicked: root.settingsRequested() }
    }
}
