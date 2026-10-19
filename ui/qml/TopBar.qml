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
        return L10n.t("settings.providerAuto")
    }
    function backendStatus(id) {
        const revision = AppInfo.providersRevision
        if (AppInfo.backendInitializing) return L10n.t("settings.providerChecking")
        const key = String(id || "auto").toLowerCase()
        return AppInfo.backendAvailable(key) ? L10n.t("top.ready") : L10n.t("settings.providerUnavailable")
    }
    function chooseProvider(id) {
        Analysis.providerChoice = id
        providerMenu.close()
    }

    Rectangle { anchors.fill: parent; color: Theme.canvas }
    Item {
        id: brand
        anchors.left: parent.left; anchors.leftMargin: 26; anchors.verticalCenter: parent.verticalCenter
        width: 222; height: 40
        HoverHandler { id: brandHover }
        Rectangle {
            x: brandHover.hovered ? 5 : 0; y: 9; width: 10; height: 23; radius: 3
            color: Theme.accentMuted; border.color: Theme.accent
            rotation: brandHover.hovered ? -8 : 0
            Behavior on x { NumberAnimation { duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic } }
            Behavior on rotation { NumberAnimation { duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic } }
        }
        Rectangle {
            x: brandHover.hovered ? 19 : 16; y: 9; width: 10; height: 23; radius: 3
            color: Theme.sageMuted; border.color: Theme.sage
            rotation: brandHover.hovered ? 8 : 0
            Behavior on x { NumberAnimation { duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic } }
            Behavior on rotation { NumberAnimation { duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic } }
        }
        Text {
            x: 42; anchors.verticalCenter: parent.verticalCenter
            text: "Parallel Finder"; color: Theme.textPrimary; font.family: Theme.fontFamily
            font.pixelSize: 19; font.weight: Font.DemiBold; font.letterSpacing: -0.6
        }
    }
    RowLayout {
        anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 14
        Text { visible: root.busy || AppInfo.backendInitializing; font.family: Theme.fontFamily; Layout.alignment: Qt.AlignVCenter; text: root.busy ? L10n.t("top.analyzing") : L10n.t("settings.providerChecking"); color: root.busy ? Theme.accentText : Theme.textSecondary; font.pixelSize: 11 }
        Rectangle {
            id: providerStatus
            objectName: "topProviderSelector"
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 226
            Layout.minimumWidth: 168
            Layout.maximumWidth: 250
            width: 226; height: 28; radius: 8
            color: providerMenu.visible ? Theme.accentMuted : Theme.surfaceRaised; border.color: providerMenu.visible ? Theme.accent : Theme.hairline
            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                Rectangle { Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: !AppInfo.backendInitializing && AppInfo.backendAvailable(Analysis.providerChoice) ? "#22C55E" : Theme.textDisabled
                }
                Text { font.family: Theme.fontFamily;
                    id: gpuLabel
                    objectName: "gpuStatusLabel"
                    Layout.fillWidth: true
                    text: Analysis.providerChoice === "auto"
                        ? backendLabel(AppInfo.gpuBackend) + " · " + backendStatus("auto")
                        : backendLabel(Analysis.providerChoice) + " · " + backendStatus(Analysis.providerChoice)
                    color: !AppInfo.backendInitializing && Analysis.providerChoice !== "auto" && !AppInfo.backendAvailable(Analysis.providerChoice)
                        ? Theme.accentText : Theme.textPrimary
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
            Popup {
                id: providerMenu
                objectName: "providerMenu"
                y: providerStatus.height + 6
                width: 278; padding: 6
                implicitHeight: contentItem.implicitHeight + 12
                background: Rectangle { color: Theme.tooltipSurface; radius: Theme.radiusCard; border.color: Theme.hairlineStrong }
                enter: Transition { NumberAnimation { property: "scale"; from: Theme.motionPopupScale; to: 1; duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic } }
                contentItem: Column {
                  spacing: 3
                  Repeater {
                    model: ["auto", "cuda", "tensorrt", "dml", "cpu"]
                    delegate: ItemDelegate {
                        required property string modelData
                        objectName: "providerItem_" + modelData
                        readonly property bool isReady: !AppInfo.backendInitializing && AppInfo.providersRevision >= 0 && AppInfo.backendAvailable(modelData)
                        readonly property bool selectedProvider: Analysis.providerChoice === modelData
                        width: 266; height: 44; hoverEnabled: true
                        enabled: isReady && !Analysis.busy && !Analysis.exportBusy
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle { Layout.preferredWidth: 5; Layout.preferredHeight: 5; radius: 3; color: parent.parent.isReady ? Theme.accentText : Theme.textDisabled }
                            Column {
                                Layout.fillWidth: true; spacing: 2
                                Text { objectName: "providerName_" + modelData; width: parent.width; text: root.backendLabel(modelData); color: Theme.textPrimary; opacity: isReady ? 1 : 0.55; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: Font.Medium; elide: Text.ElideRight }
                                Text { width: parent.width; text: root.backendStatus(modelData); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10; elide: Text.ElideRight }
                            }
                            Text { text: selectedProvider ? "✓" : ""; color: Theme.accentText; font.pixelSize: 14 }
                        }
                        background: Rectangle { radius: 7; color: parent.selectedProvider ? Theme.accentMuted : parent.hovered ? Theme.surfaceRaised : "transparent"; Behavior on color { ColorAnimation { duration: Theme.motionDuration } } }
                        onClicked: root.chooseProvider(modelData)
                    }
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
