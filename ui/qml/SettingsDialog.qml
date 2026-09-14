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
    signal advancedRequested()
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

    function applyAccuracyPreset(preset) {
        Analysis.accuracyPreset = preset
        if (preset === "fast") {
            Analysis.similarityThreshold = 0.72; Analysis.candidateThreshold = 0.40
            Analysis.repeatGap = 8.0; Analysis.sameFileGap = 3.0; Analysis.duplicateWindow = 2.0
            Analysis.noiseFactor = 1.25; Analysis.maxUniqueResults = 50; Analysis.timeWeight = 0.10
        } else if (preset === "precise") {
            Analysis.similarityThreshold = 0.90; Analysis.candidateThreshold = 0.70
            Analysis.repeatGap = 4.0; Analysis.sameFileGap = 1.5; Analysis.duplicateWindow = 1.0
            Analysis.noiseFactor = 0.70; Analysis.maxUniqueResults = 200; Analysis.timeWeight = 0.40
        } else {
            Analysis.similarityThreshold = 0.85; Analysis.candidateThreshold = 0.55
            Analysis.repeatGap = 6.0; Analysis.sameFileGap = 2.0; Analysis.duplicateWindow = 1.5
            Analysis.noiseFactor = 1.0; Analysis.maxUniqueResults = 100; Analysis.timeWeight = 0.25
        }
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
                        Rectangle { width: parent.width; color: Theme.panelAlt; radius: Theme.radiusButton; border.color: Theme.border; implicitHeight: profileBody.implicitHeight + 24
                            Column { id: profileBody; anchors.fill: parent; anchors.margins: 12; spacing: 9
                                Text { text: L10n.t("search.accuracyGroup"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                                Flow { width: parent.width; spacing: 6
                                    PfButton { width: 112; compact: true; text: L10n.t("search.presetFast"); quiet: Analysis.accuracyPreset !== "fast"; onClicked: root.applyAccuracyPreset("fast") }
                                    PfButton { width: 82; compact: true; text: L10n.t("search.presetBalanced"); quiet: Analysis.accuracyPreset !== "balanced"; onClicked: root.applyAccuracyPreset("balanced") }
                                    PfButton { width: 124; compact: true; text: L10n.t("search.presetPrecise"); quiet: Analysis.accuracyPreset !== "precise"; onClicked: root.applyAccuracyPreset("precise") }
                                }
                                Text { text: L10n.t("search.frameProcessing"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                                Flow { width: parent.width; spacing: 6
                                    PfButton { width: 82; compact: true; text: L10n.t("search.fast"); quiet: Analysis.qualityProfile !== "fast"; onClicked: Analysis.qualityProfile = "fast" }
                                    PfButton { width: 82; compact: true; text: L10n.t("search.medium"); quiet: Analysis.qualityProfile !== "medium"; onClicked: Analysis.qualityProfile = "medium" }
                                    PfButton { width: 124; compact: true; text: L10n.t("search.maximum"); quiet: Analysis.qualityProfile !== "maximum"; onClicked: Analysis.qualityProfile = "maximum" }
                                }
                                Text { width: parent.width; text: L10n.t("settings.advancedHint"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
                                PfButton { width: parent.width; compact: true; text: L10n.t("settings.openAdvanced"); quiet: true; onClicked: root.advancedRequested() }
                            }
                        }
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
                        Text { width: parent.width; text: Analysis.modelDownloading ? L10n.t("settings.modelDownloading") : (Analysis.modelStatus.length ? L10n.status(Analysis.modelStatus) : L10n.t("settings.modelHint")); color: Analysis.modelDownloading ? Theme.accent : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.cache"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 36; spacing: 12
                            Text { id: cacheLabel; width: 135; text: L10n.t("settings.cachePath"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; ToolTip.visible: cacheHelp.hovered; ToolTip.text: L10n.t("settings.cacheHint"); ToolTip.delay: 350 }
                            HoverHandler { id: cacheHelp }
                            PfTextField { width: parent.width - 147; text: Analysis.cachePath; placeholderText: L10n.t("settings.cachePlaceholder"); Accessible.name: L10n.t("settings.cachePath"); onEditingFinished: Analysis.setCachePath(text) }
                        }
                        PfSliderField { width: parent.width; label: L10n.t("settings.cacheLimit"); from: 0.25; to: 128; stepSize: 0.25; value: Analysis.cacheLimitGb; decimals: 2; suffix: "GB"; tooltipText: L10n.t("settings.cacheLimitHint"); Accessible.name: L10n.t("settings.cacheLimit"); onValueEdited: Analysis.setCacheLimitGb(nextValue) }
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
                        PfSliderField { width: parent.width; label: L10n.t("settings.surfaceOpacity"); from: 0.72; to: 1.0; stepSize: 0.01; value: Theme.surfaceOpacity; displayScale: 100; decimals: 0; suffix: "%"; tooltipText: L10n.t("settings.surfaceOpacityHint"); Accessible.name: L10n.t("settings.surfaceOpacity"); onValueEdited: { Theme.surfaceOpacity = nextValue; customizationStore.surfaceOpacity = nextValue } }
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
