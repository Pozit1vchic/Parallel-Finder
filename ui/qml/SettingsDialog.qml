import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import QtCore
import PfUi
import PfUiBridge

Popup {
    id: root
    property var rootWindow
    property bool userPositioned: false
    property string themeStatus: ""
    readonly property var providerIds: ["auto", "dml", "cuda", "tensorrt", "cpu"]
    readonly property var providerLabels: [
        L10n.t("settings.providerAuto"),
        L10n.t("settings.providerDirectMl"),
        L10n.t("settings.providerCuda"),
        L10n.t("settings.providerTensorRt"),
        L10n.t("settings.providerCpu")
    ]
    modal: true
    focus: true
    padding: 0
    width: Math.min(680, rootWindow ? rootWindow.width - 32 : 640)
    height: Math.min(620, rootWindow ? rootWindow.height - 32 : 580)
    x: 0
    y: 0
    transformOrigin: Item.Center
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: 150; easing.type: Easing.InCubic }
        }
    }
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.overlayDim }
    background: Rectangle {
        color: Theme.heroPanel
        radius: Theme.radiusOverlay
        border.color: Theme.hairline
        layer.enabled: true
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowOverlay; shadowOpacity: 0.78; shadowBlur: 1.0; shadowHorizontalOffset: 0; shadowVerticalOffset: 20 }
    }

    Settings {
        id: customizationStore
        category: "ParallelFinder/customization"
        property string language: "ru"
        property string fontFamily: "Segoe UI"
        property string customFontPath: ""
        property real surfaceOpacity: 1.0
    }
    FontLoader {
        id: customFont
        source: customizationStore.customFontPath
        onStatusChanged: if (status === FontLoader.Ready) {
            Theme.fontFamily = name
            customizationStore.fontFamily = name
            root.themeStatus = name
        }
    }
    FileDialog {
        id: fontDialog
        title: L10n.t("settings.fontAdd")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Fonts (*.ttf *.otf *.woff *.woff2)", L10n.t("dialog.allFiles")]
        onAccepted: {
            customizationStore.customFontPath = selectedFile.toLocalFile()
            customFont.source = customizationStore.customFontPath
        }
    }
    FolderDialog {
        id: cacheDialog
        title: L10n.t("settings.chooseCache")
        onAccepted: Analysis.setCachePath(selectedFolder.toLocalFile())
    }

    Component.onCompleted: {
        L10n.language = customizationStore.language
        Theme.surfaceOpacity = customizationStore.surfaceOpacity
        if (!customizationStore.customFontPath.length) Theme.fontFamily = customizationStore.fontFamily
        root.centerInWindow()
    }
    onClosed: {
        customizationStore.language = L10n.language
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.surfaceOpacity = Theme.surfaceOpacity
    }
    onOpened: centerInWindow()
    Keys.onEscapePressed: root.close()

    function centerInWindow() {
        if (!rootWindow || root.userPositioned) return
        root.x = Math.round((rootWindow.width - root.width) / 2)
        root.y = Math.round((rootWindow.height - root.height) / 2)
    }
    function resetAppearance() {
        Theme.fontFamily = "Segoe UI"
        Theme.surfaceOpacity = 1.0
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.customFontPath = ""
        customizationStore.surfaceOpacity = Theme.surfaceOpacity
        root.themeStatus = L10n.t("settings.resetDone")
    }

    contentItem: Column {
        spacing: 0
        Rectangle {
            width: parent.width
            height: 58
            color: Theme.surfaceRaised
            radius: Theme.radiusOverlay
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
            Text { anchors.left: parent.left; anchors.leftMargin: 22; anchors.verticalCenter: parent.verticalCenter; text: L10n.t("settings.windowTitle"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 13; font.weight: Font.DemiBold }
            PfIconButton { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: root.close() }
            MouseArea {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.rightMargin: 56
                property real pressX
                property real pressY
                property real startX
                property real startY
                cursorShape: Qt.SizeAllCursor
                onPressed: { pressX = mouse.x; pressY = mouse.y; startX = root.x; startY = root.y; root.userPositioned = true }
                onPositionChanged: if (pressed && rootWindow) {
                    root.x = Math.max(12, Math.min(rootWindow.width - root.width - 12, startX + mouse.x - pressX))
                    root.y = Math.max(12, Math.min(rootWindow.height - root.height - 12, startY + mouse.y - pressY))
                }
            }
        }
        TabBar {
            id: tabs
            width: parent.width
            height: 46
            background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.hairline }
            TabButton {
                id: analysisTab
                text: L10n.t("settings.tabAnalysis")
                contentItem: Text { text: analysisTab.text; color: analysisTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: analysisTab.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: analysisTab.checked ? Theme.panelAlt : "transparent"; border.color: analysisTab.checked ? Theme.accent : "transparent"; border.width: analysisTab.checked ? 1 : 0; radius: Theme.radiusButton }
            }
            TabButton {
                id: customizationTab
                text: L10n.t("settings.tabAppearance")
                contentItem: Text { text: customizationTab.text; color: customizationTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: customizationTab.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: customizationTab.checked ? Theme.panelAlt : "transparent"; border.color: customizationTab.checked ? Theme.accent : "transparent"; border.width: customizationTab.checked ? 1 : 0; radius: Theme.radiusButton }
            }
        }
        StackLayout { id: pages; width: parent.width; height: parent.height - 104; currentIndex: tabs.currentIndex
            Item {
                Flickable { anchors.fill: parent; anchors.margins: 22; clip: true; contentWidth: width; contentHeight: analysisBody.implicitHeight + 30; boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout { id: analysisBody; width: parent.width; spacing: 14
                        Text { text: L10n.t("settings.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                        Text { text: L10n.t("settings.subtitle"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.environment"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        RowLayout { Layout.fillWidth: true; spacing: 12
                            Text { Layout.preferredWidth: 128; text: L10n.t("settings.provider"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; ToolTip.visible: providerHelp.hovered; ToolTip.text: L10n.t("settings.providerHint"); ToolTip.delay: 350; elide: Text.ElideRight }
                            HoverHandler { id: providerHelp }
                            PfComboBox { id: provider; Layout.preferredWidth: 180; Layout.fillWidth: true; model: root.providerLabels; currentIndex: Math.max(0, root.providerIds.indexOf(Analysis.providerChoice)); Accessible.name: L10n.t("settings.provider"); onActivated: Analysis.providerChoice = root.providerIds[currentIndex] }
                        }
                        Text { Layout.fillWidth: true; text: AppInfo.gpuSummary; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        Text {
                            Layout.fillWidth: true
                            text: Analysis.providerChoice === "auto"
                                ? L10n.t("settings.providerHint")
                                : (AppInfo.backendAvailable(Analysis.providerChoice)
                                    ? "✓ " + Analysis.providerChoice.toUpperCase() + " доступен"
                                    : "Провайдер недоступен: " + (AppInfo.backendReason(Analysis.providerChoice) || "нет совместимого runtime"))
                            color: AppInfo.backendAvailable(Analysis.providerChoice) ? Theme.sage : Theme.accent
                            font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap
                        }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.cache"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        RowLayout { Layout.fillWidth: true; spacing: 8
                            PfTextField { Layout.fillWidth: true; text: Analysis.cachePath; placeholderText: L10n.t("settings.cachePlaceholder"); Accessible.name: L10n.t("settings.cachePath"); onEditingFinished: Analysis.setCachePath(text) }
                            PfButton { Layout.preferredWidth: 96; compact: true; text: L10n.t("settings.choose"); quiet: true; onClicked: cacheDialog.open() }
                        }
                        PfSliderField { Layout.fillWidth: true; label: L10n.t("settings.cacheLimit"); from: 0.25; to: 128; stepSize: 0.25; value: Analysis.cacheLimitGb; decimals: 2; suffix: "GB"; tooltipText: L10n.t("settings.cacheLimitHint"); Accessible.name: L10n.t("settings.cacheLimit"); onValueEdited: Analysis.setCacheLimitGb(nextValue) }
                        PfSliderField { Layout.fillWidth: true; label: L10n.t("settings.processingThreads"); from: 0; to: 64; stepSize: 1; value: Analysis.processingThreads; decimals: 0; integer: true; tooltipText: L10n.t("settings.processingThreadsHint"); Accessible.name: L10n.t("settings.processingThreads"); onValueEdited: Analysis.setProcessingThreads(Math.round(nextValue)) }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.systemHint"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                }
            }
            Item {
                Flickable { anchors.fill: parent; anchors.margins: 22; clip: true; contentWidth: width; contentHeight: appearanceBody.implicitHeight + 30; boundsBehavior: Flickable.StopAtBounds
                    Column { id: appearanceBody; width: parent.width; spacing: 14
                        Text { text: L10n.t("settings.appearanceTitle"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                        Text { text: L10n.t("settings.appearanceSubtitle"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.language"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfComboBox { width: parent.width; model: [L10n.t("settings.languageRussian"), L10n.t("settings.languageEnglish")]; currentIndex: L10n.language === "en" ? 1 : 0; Accessible.name: L10n.t("settings.language"); onActivated: { L10n.language = currentIndex === 1 ? "en" : "ru"; customizationStore.language = L10n.language } }
                        Text { text: L10n.t("settings.font"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        RowLayout { width: parent.width; spacing: 8
                            PfComboBox { Layout.fillWidth: true; model: ["Segoe UI", "Arial", "Verdana"]; currentIndex: Math.max(0, model.indexOf(Theme.fontFamily)); Accessible.name: L10n.t("settings.font"); onActivated: { Theme.fontFamily = currentText; customizationStore.fontFamily = currentText; customizationStore.customFontPath = "" } }
                            PfButton { Layout.preferredWidth: 132; text: L10n.t("settings.fontAdd"); quiet: true; onClicked: fontDialog.open() }
                        }
                        Text { visible: customFont.status === FontLoader.Ready; text: L10n.t("settings.fontLoaded") + ": " + customFont.name; color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
                        Text { text: L10n.t("settings.surfaceOpacity"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfSliderField { width: parent.width; label: L10n.t("settings.surfaceOpacity"); from: 0.72; to: 1.0; stepSize: 0.01; value: Theme.surfaceOpacity; displayScale: 100; decimals: 0; suffix: "%"; tooltipText: L10n.t("settings.surfaceOpacityHint"); Accessible.name: L10n.t("settings.surfaceOpacity"); onValueEdited: { Theme.surfaceOpacity = nextValue; customizationStore.surfaceOpacity = nextValue } }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        PfButton { width: parent.width; text: L10n.t("settings.resetAppearance"); quiet: true; onClicked: root.resetAppearance() }
                        Text { width: parent.width; text: root.themeStatus || L10n.t("settings.profileSaved"); color: root.themeStatus ? Theme.accent : Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }
}
