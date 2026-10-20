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
            color: providerMenu.visible || gpuHover.hovered ? Theme.accentMuted : Theme.surfaceRaised
            border.color: providerMenu.visible || gpuHover.hovered ? Theme.accent : Theme.hairline
            Behavior on color { ColorAnimation { duration: Theme.reducedMotion ? 0 : 260; easing.type: Easing.InOutSine } }
            Behavior on border.color { ColorAnimation { duration: Theme.reducedMotion ? 0 : 260; easing.type: Easing.InOutSine } }
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
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: providerMenu.visible ? providerMenu.close() : providerMenu.open() }
            Popup {
                id: providerMenu
                objectName: "providerMenu"
                x: providerStatus.width - width
                y: providerStatus.height + 6
                width: 296; padding: 7
                implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                transformOrigin: Popup.TopRight
                background: Rectangle { color: Theme.tooltipSurface; radius: Theme.radiusCard; border.color: Theme.hairlineStrong }
                enter: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "scale"; from: Theme.reducedMotion ? 1 : 0.988; to: 1; duration: Theme.reducedMotion ? 0 : 360; easing.type: Easing.InOutSine }
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.reducedMotion ? 0 : 280; easing.type: Easing.InOutSine }
                    }
                }
                exit: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "scale"; to: Theme.reducedMotion ? 1 : 0.988; duration: Theme.reducedMotion ? 0 : 240; easing.type: Easing.InOutSine }
                        NumberAnimation { property: "opacity"; to: 0; duration: Theme.reducedMotion ? 0 : 240; easing.type: Easing.InOutSine }
                    }
                }
                contentItem: Column {
                  spacing: 3
                  Repeater {
                    model: ["auto", "cuda", "tensorrt", "dml", "cpu"]
                    delegate: ItemDelegate {
                        id: providerRow
                        required property string modelData
                        objectName: "providerItem_" + modelData
                        readonly property bool isReady: !AppInfo.backendInitializing && AppInfo.providersRevision >= 0 && AppInfo.backendAvailable(modelData)
                        readonly property bool selectedProvider: Analysis.providerChoice === modelData
                        width: providerMenu.availableWidth; height: 48; hoverEnabled: true
                        leftPadding: 12; rightPadding: 12; topPadding: 7; bottomPadding: 7
                        enabled: isReady && !Analysis.busy && !Analysis.exportBusy
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle { Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 5; Layout.preferredHeight: 5; radius: 3; color: providerRow.isReady ? Theme.accentText : Theme.textDisabled }
                            Column {
                                Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; spacing: 3
                                Text { objectName: "providerName_" + modelData; width: parent.width; text: root.backendLabel(modelData); color: Theme.textPrimary; opacity: isReady ? 1 : 0.55; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: Font.Medium; elide: Text.ElideRight }
                                Text {
                                    width: parent.width
                                    text: AppInfo.backendInitializing ? L10n.t("settings.providerChecking")
                                        : providerRow.isReady ? L10n.t("top.ready")
                                        : L10n.t("settings.providerUnavailable").split(":")[0]
                                    color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10
                                }
                            }
                            Text {
                                Layout.preferredWidth: 18; Layout.alignment: Qt.AlignVCenter
                                text: "✓"; color: Theme.accentText; font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                opacity: providerRow.selectedProvider ? 1 : 0
                                scale: providerRow.selectedProvider ? 1 : 0.9
                                Behavior on opacity { NumberAnimation { duration: Theme.reducedMotion ? 0 : 260; easing.type: Easing.InOutSine } }
                                Behavior on scale { NumberAnimation { duration: Theme.reducedMotion ? 0 : 300; easing.type: Easing.InOutSine } }
                            }
                        }
                        background: Rectangle {
                            radius: 8
                            color: providerRow.selectedProvider ? Theme.accentMuted : providerRow.hovered ? Theme.surfaceRaised : "transparent"
                            border.width: 1
                            border.color: providerRow.hovered ? Theme.hairlineStrong : "transparent"
                            Behavior on color { ColorAnimation { duration: Theme.reducedMotion ? 0 : 260; easing.type: Easing.InOutSine } }
                            Behavior on border.color { ColorAnimation { duration: Theme.reducedMotion ? 0 : 260; easing.type: Easing.InOutSine } }
                        }
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
