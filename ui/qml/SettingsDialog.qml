import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import Qt.labs.settings
import PfUi
import PfUiBridge

Popup {
    id: root
    property Item rootWindow
    property string selectedToken: "accent"
    property bool customizationMode: false
    modal: true
    focus: true
    padding: 0
    width: Math.min(760, rootWindow ? rootWindow.width - 40 : 720)
    height: Math.min(720, rootWindow ? rootWindow.height - 40 : 680)
    x: rootWindow ? Math.round((rootWindow.width - width) / 2) : 0
    y: rootWindow ? Math.round((rootWindow.height - height) / 2) : 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.74) }
    enter: Transition { NumberAnimation { properties: "opacity,scale"; from: 0.94; to: 1; duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { properties: "opacity,scale"; to: 0.94; duration: Theme.motionDuration } }
    background: Rectangle {
        color: Theme.heroPanel
        radius: Theme.radiusOverlay
        border.color: Theme.hairline
        layer.enabled: true
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.82); shadowBlur: 1.0; shadowVerticalOffset: 20 }
    }

    Settings {
        id: customizationStore
        category: "ParallelFinder/customization"
        property color accent: "#D97757"
        property color sage: "#7C9885"
        property string fontFamily: "Segoe UI"
        property bool reducedMotion: false
    }
    ColorDialog {
        id: colorDialog
        title: L10n.t("settings.colorDialog")
        selectedColor: root.selectedToken === "accent" ? Theme.accent : Theme.sage
        onAccepted: {
            if (root.selectedToken === "accent") { Theme.accent = selectedColor; customizationStore.accent = selectedColor }
            else { Theme.sage = selectedColor; customizationStore.sage = selectedColor }
        }
    }
    FileDialog { id: modelDialog; title: L10n.t("settings.modelPath"); fileMode: FileDialog.OpenFile; nameFilters: ["ONNX (*.onnx)", L10n.t("dialog.allFiles")]; onAccepted: Analysis.setModelPath(selectedFile.toString().replace(/^file:\/\//, "")) }
    FolderDialog { id: cacheDialog; title: L10n.t("settings.cachePath"); onAccepted: Analysis.setCachePath(selectedFolder.toString().replace(/^file:\/\//, "")) }

    Component.onCompleted: {
        Theme.accent = customizationStore.accent
        Theme.sage = customizationStore.sage
        Theme.fontFamily = customizationStore.fontFamily
        Theme.reducedMotion = customizationStore.reducedMotion
    }
    onClosed: {
        customizationStore.accent = Theme.accent
        customizationStore.sage = Theme.sage
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.reducedMotion = Theme.reducedMotion
    }
    Keys.onEscapePressed: root.close()

    function resetAppearance() {
        Theme.accent = "#D97757"
        Theme.sage = "#7C9885"
        Theme.fontFamily = "Segoe UI"
        Theme.reducedMotion = false
        customizationStore.accent = Theme.accent
        customizationStore.sage = Theme.sage
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.reducedMotion = Theme.reducedMotion
    }

    contentItem: Column {
        spacing: 0
        Rectangle {
            width: parent.width
            height: 58
            color: Theme.surfaceRaised
            radius: Theme.radiusOverlay
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
            Text { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; text: L10n.t("settings.windowTitle"); color: Theme.textPrimary; font.pixelSize: 13; font.weight: Font.DemiBold }
            PfIconButton { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; iconSource: "qrc:/qt/qml/PfUi/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: root.close() }
            MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.rightMargin: 58; property real pressX; property real pressY
                onPressed: { pressX = mouse.x; pressY = mouse.y }
                onPositionChanged: if (pressed && rootWindow) { root.x = Math.max(12, Math.min(rootWindow.width - root.width - 12, root.x + mouse.x - pressX)); root.y = Math.max(12, Math.min(rootWindow.height - root.height - 12, root.y + mouse.y - pressY)) }
            }
        }
        TabBar {
            id: tabs
            width: parent.width
            height: 48
            background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.hairline }
            TabButton { text: L10n.t("settings.tabAnalysis"); Accessible.name: L10n.t("settings.tabAnalysis") }
            TabButton { text: L10n.t("settings.tabAppearance"); Accessible.name: L10n.t("settings.tabAppearance") }
            TabButton { text: L10n.t("settings.tabKeyboard"); Accessible.name: L10n.t("settings.tabKeyboard") }
        }
        StackLayout { id: pages; width: parent.width; height: parent.height - 106; currentIndex: tabs.currentIndex
            Item {
                Flickable { anchors.fill: parent; clip: true; contentWidth: width; contentHeight: analysisBody.implicitHeight + 48; boundsBehavior: Flickable.StopAtBounds; ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Column { id: analysisBody; width: parent.width - 48; x: 24; y: 24; spacing: 12
                        Text { text: L10n.t("settings.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                        Text { text: L10n.t("settings.subtitle"); color: Theme.textSecondary; font.pixelSize: 12 }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.environment"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 36; spacing: 12
                            Text { width: 135; text: L10n.t("settings.provider"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            ComboBox { id: provider; width: 190; height: 32; model: ["Auto", "TensorRT", "CUDA", "DirectML", "CPU"]; currentIndex: ["auto", "tensorrt", "cuda", "dml", "cpu"].indexOf(Analysis.providerChoice); Accessible.name: L10n.t("settings.provider"); onActivated: Analysis.providerChoice = ["auto", "tensorrt", "cuda", "dml", "cpu"][currentIndex] }
                            Text { text: AppInfo.gpuSummary; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; width: parent.width - 345 }
                        }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.modelPath"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            TextField { width: parent.width - 230; text: Analysis.modelPath; placeholderText: "models / yolo26m-pose.onnx"; Accessible.name: L10n.t("settings.modelPath"); onEditingFinished: Analysis.setModelPath(text) }
                            PfButton { width: 80; text: L10n.t("settings.browse"); quiet: true; onClicked: modelDialog.open() }
                        }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.cachePath"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            TextField { width: parent.width - 230; text: Analysis.cachePath; placeholderText: "%LocalAppData%/ParallelFinder/cache"; Accessible.name: L10n.t("settings.cachePath"); onEditingFinished: Analysis.setCachePath(text) }
                            PfButton { width: 80; text: L10n.t("settings.browse"); quiet: true; onClicked: cacheDialog.open() }
                        }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.cacheLimit"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            PfSlider { width: parent.width - 240; from: 0.25; to: 128; stepSize: 0.25; value: Analysis.cacheLimitGb; Accessible.name: L10n.t("settings.cacheLimit"); onValueChanged: if (Math.abs(Analysis.cacheLimitGb - value) > 0.001) Analysis.cacheLimitGb = value }
                            Text { width: 80; text: Analysis.cacheLimitGb.toFixed(2); color: Theme.accent; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.scene"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.sceneThreshold"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            PfSlider { width: parent.width - 240; from: 1; to: 255; stepSize: 1; value: Analysis.sceneThreshold; Accessible.name: L10n.t("settings.sceneThreshold"); onValueChanged: if (Math.abs(Analysis.sceneThreshold - value) > 0.1) Analysis.sceneThreshold = value }
                            Text { width: 80; text: Math.round(Analysis.sceneThreshold); color: Theme.accent; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.matcher"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Text { text: L10n.t("settings.matcherHint"); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                        PfCheckBox { text: L10n.t("settings.reducedMotion"); checked: Theme.reducedMotion; onToggled: Theme.reducedMotion = checked }
                        Row { width: parent.width; spacing: 8
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.reset"); quiet: true; onClicked: root.resetRequested() }
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.saved"); quiet: true; enabled: false }
                        }
                    }
                }
            }
            Item {
                Flickable { anchors.fill: parent; clip: true; contentWidth: width; contentHeight: appearanceBody.implicitHeight + 48; boundsBehavior: Flickable.StopAtBounds; ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Column { id: appearanceBody; width: parent.width - 48; x: 24; y: 24; spacing: 14
                        Text { text: L10n.t("settings.appearanceTitle"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                        Text { text: L10n.t("settings.appearanceSubtitle"); color: Theme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.palette"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; spacing: 8
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.accentAction"); quiet: root.selectedToken !== "accent"; onClicked: root.selectedToken = "accent" }
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.sageContext"); sageAction: true; quiet: root.selectedToken !== "sage"; onClicked: root.selectedToken = "sage" }
                        }
                        Rectangle { width: parent.width; height: 120; radius: Theme.radiusCard; color: Theme.well; border.width: root.customizationMode ? 2 : 1; border.color: root.customizationMode ? (root.selectedToken === "accent" ? Theme.accent : Theme.sage) : Theme.hairline
                            Row { anchors.centerIn: parent; spacing: 12
                                Rectangle { width: 124; height: 52; radius: 9; color: Theme.accent; Text { anchors.centerIn: parent; text: "A"; color: Theme.canvas; font.pixelSize: 16; font.weight: Font.DemiBold } }
                                Rectangle { width: 124; height: 52; radius: 9; color: Theme.sage; Text { anchors.centerIn: parent; text: "B"; color: Theme.canvas; font.pixelSize: 16; font.weight: Font.DemiBold } }
                            }
                            MouseArea { anchors.fill: parent; onClicked: root.customizationMode = !root.customizationMode }
                        }
                        Row { width: parent.width; spacing: 8
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.changeColor"); onClicked: colorDialog.open() }
                            PfButton { width: (parent.width - 8) / 2; text: root.customizationMode ? L10n.t("settings.editEnabled") : L10n.t("settings.enableEdit"); quiet: !root.customizationMode; onClicked: root.customizationMode = !root.customizationMode }
                        }
                        Text { text: L10n.t("settings.font"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        ComboBox { width: parent.width; model: ["Segoe UI", "Arial", "Verdana"]; currentIndex: model.indexOf(Theme.fontFamily); Accessible.name: L10n.t("settings.font"); onActivated: { Theme.fontFamily = currentText; customizationStore.fontFamily = currentText } }
                        PfCheckBox { text: L10n.t("settings.reducedMotion"); checked: Theme.reducedMotion; onToggled: { Theme.reducedMotion = checked; customizationStore.reducedMotion = checked } }
                        Row { width: parent.width; spacing: 8
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.resetAppearance"); quiet: true; onClicked: root.resetAppearance() }
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.exportTheme"); quiet: true; enabled: false }
                        }
                        Text { text: L10n.t("settings.profileSaved"); color: Theme.textDisabled; font.pixelSize: 10 }
                    }
                }
            }
            Item {
                Column { anchors.fill: parent; anchors.margins: 24; spacing: 14
                    Text { text: L10n.t("settings.a11yTitle"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                    Text { text: L10n.t("settings.a11ySubtitle"); color: Theme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
                    Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                    Repeater { model: [{key: "Tab / Shift+Tab", value: L10n.t("settings.keyTab")}, {key: "↑ / ↓", value: L10n.t("settings.keyArrows")}, {key: "Enter", value: L10n.t("settings.keyEnter")}, {key: "Esc", value: L10n.t("settings.keyEscape")}]
                        delegate: Row { width: parent.width; height: 34; spacing: 18
                            Rectangle { width: 132; height: 28; radius: 6; color: Theme.surfaceRaised; border.color: Theme.hairline; Text { anchors.centerIn: parent; text: modelData.key; color: Theme.accent; font.pixelSize: 11; font.weight: Font.DemiBold } }
                            Text { text: modelData.value; color: Theme.textSecondary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                    Item { width: 1; height: 1 }
                    Text { text: L10n.t("settings.a11yHint"); color: Theme.textDisabled; font.pixelSize: 10; wrapMode: Text.WordWrap; width: parent.width }
                }
            }
        }
    }
    signal resetRequested()
}
