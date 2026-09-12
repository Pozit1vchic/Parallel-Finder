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
    property var modelFiles: [
        "yolo8n-pose.onnx", "yolo8m-pose.onnx", "yolo8s-pose.onnx", "yolo8l-pose.onnx", "yolo8x-pose.onnx",
        "yolo11n-pose.onnx", "yolo11m-pose.onnx", "yolo11s-pose.onnx", "yolo11l-pose.onnx", "yolo11x-pose.onnx",
        "yolo26n-pose.onnx", "yolo26m-pose.onnx", "yolo26s-pose.onnx", "yolo26m-pose-640-b1.onnx", "yolo26l-pose.onnx", "yolo26x-pose.onnx"
    ]
    property var modelLabels: [
        "YOLO 8 · nano", "YOLO 8 · medium", "YOLO 8 · small", "YOLO 8 · large", "YOLO 8 · xlarge",
        "YOLO 11 · nano", "YOLO 11 · medium", "YOLO 11 · small", "YOLO 11 · large", "YOLO 11 · xlarge",
        "YOLO 26 · nano", "YOLO 26 · medium", "YOLO 26 · small", "YOLO 26 · medium · 640", "YOLO 26 · large", "YOLO 26 · xlarge"
    ]
    modal: true
    focus: true
    padding: 0
    width: Math.min(680, rootWindow ? rootWindow.width - 32 : 640)
    height: Math.min(620, rootWindow ? rootWindow.height - 32 : 580)
    x: 0
    y: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.overlayDim }
    background: Rectangle {
        color: Theme.heroPanel
        radius: Theme.radiusOverlay
        border.color: Theme.hairline
        layer.enabled: true
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowOverlay; shadowBlur: 1.0; shadowVerticalOffset: 18 }
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
                    Column { id: analysisBody; width: parent.width; spacing: 12
                        Text { text: L10n.t("settings.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                        Text { text: L10n.t("settings.subtitle"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.environment"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 36; spacing: 12
                            Text { id: providerLabel; width: 135; text: L10n.t("settings.provider"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; ToolTip.visible: providerHelp.hovered; ToolTip.text: L10n.t("settings.providerHint"); ToolTip.delay: 350 }
                            HoverHandler { id: providerHelp }
                            PfComboBox { id: provider; width: 190; model: ["Auto", "TensorRT", "CUDA", "DirectML", "CPU"]; currentIndex: ["auto", "tensorrt", "cuda", "dml", "cpu"].indexOf(Analysis.providerChoice); Accessible.name: L10n.t("settings.provider"); onActivated: Analysis.providerChoice = ["auto", "tensorrt", "cuda", "dml", "cpu"][currentIndex] }
                            Text { text: AppInfo.gpuSummary; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; width: parent.width - 345 }
                        }
                        Text { text: L10n.t("settings.models"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 36; spacing: 12
                            Text { id: modelLabel; width: 135; text: L10n.t("settings.poseModel"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; ToolTip.visible: modelHelp.hovered; ToolTip.text: L10n.t("settings.modelHint"); ToolTip.delay: 350 }
                            HoverHandler { id: modelHelp }
                            PfComboBox { id: modelBox; width: parent.width - 147; model: root.modelLabels; currentIndex: Math.max(0, root.modelFiles.indexOf(Analysis.modelChoice)); Accessible.name: L10n.t("settings.poseModel"); onActivated: Analysis.selectModel(root.modelFiles[currentIndex]) }
                        }
                        Text { width: parent.width; text: Analysis.modelDownloading ? L10n.t("settings.modelDownloading") : (Analysis.modelStatus.length ? Analysis.modelStatus : L10n.t("settings.modelHint")); color: Analysis.modelDownloading ? Theme.accent : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.cache"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 36; spacing: 12
                            Text { id: cacheLabel; width: 135; text: L10n.t("settings.cachePath"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; ToolTip.visible: cacheHelp.hovered; ToolTip.text: L10n.t("settings.cacheHint"); ToolTip.delay: 350 }
                            HoverHandler { id: cacheHelp }
                            PfTextField { width: parent.width - 147; text: Analysis.cachePath; placeholderText: L10n.t("settings.cachePlaceholder"); Accessible.name: L10n.t("settings.cachePath"); onEditingFinished: Analysis.setCachePath(text) }
                        }
                        Row { width: parent.width; height: 32; spacing: 12
                            Text { id: cacheLimitLabel; width: 135; text: L10n.t("settings.cacheLimit"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; ToolTip.visible: cacheLimitHelp.hovered; ToolTip.text: L10n.t("settings.cacheLimitHint"); ToolTip.delay: 350 }
                            HoverHandler { id: cacheLimitHelp }
                            PfSlider { width: parent.width - 220; from: 0.25; to: 128; stepSize: 0.25; value: Analysis.cacheLimitGb; Accessible.name: L10n.t("settings.cacheLimit"); onMoved: Analysis.cacheLimitGb = value }
                            Text { width: 65; text: Analysis.cacheLimitGb.toFixed(2) + " GB"; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        }
                        PfButton { width: parent.width; text: L10n.t("settings.reset"); quiet: true; onClicked: root.resetRequested() }
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
                        Row { width: parent.width; spacing: 8
                            PfComboBox { width: parent.width - 132; model: ["Segoe UI", "Arial", "Verdana"]; currentIndex: Math.max(0, model.indexOf(Theme.fontFamily)); Accessible.name: L10n.t("settings.font"); onActivated: { Theme.fontFamily = currentText; customizationStore.fontFamily = currentText; customizationStore.customFontPath = "" } }
                            PfButton { width: 124; text: L10n.t("settings.fontAdd"); quiet: true; onClicked: fontDialog.open() }
                        }
                        Text { visible: customFont.status === FontLoader.Ready; text: L10n.t("settings.fontLoaded") + ": " + customFont.name; color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
                        Text { text: L10n.t("settings.surfaceOpacity"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { id: opacityLabel; width: 170; text: L10n.t("settings.surfaceOpacity"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; ToolTip.visible: opacityHelp.hovered; ToolTip.text: L10n.t("settings.surfaceOpacityHint"); ToolTip.delay: 350 }
                            HoverHandler { id: opacityHelp }
                            PfSlider { width: parent.width - 250; from: 0.72; to: 1.0; stepSize: 0.01; value: Theme.surfaceOpacity; Accessible.name: L10n.t("settings.surfaceOpacity"); onMoved: { Theme.surfaceOpacity = value; customizationStore.surfaceOpacity = value } }
                            Text { width: 58; text: Math.round(Theme.surfaceOpacity * 100) + "%"; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        PfButton { width: parent.width; text: L10n.t("settings.resetAppearance"); quiet: true; onClicked: root.resetAppearance() }
                        Text { width: parent.width; text: root.themeStatus || L10n.t("settings.profileSaved"); color: root.themeStatus ? Theme.accent : Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }
    signal resetRequested()
}
