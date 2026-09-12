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
    property string themeStatus: ""
    property bool userPositioned: false
    modal: true
    focus: true
    padding: 0
    width: Math.min(760, rootWindow ? rootWindow.width - 40 : 720)
    height: Math.min(720, rootWindow ? rootWindow.height - 40 : 680)
    x: 0
    y: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.overlayDim }
    enter: Transition { NumberAnimation { properties: "opacity,scale"; from: 0.94; to: 1; duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { properties: "opacity,scale"; to: 0.94; duration: Theme.motionDuration } }
    background: Rectangle {
        color: Theme.heroPanel
        radius: Theme.radiusOverlay
        border.color: Theme.hairline
        layer.enabled: true
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowOverlay; shadowBlur: 1.0; shadowVerticalOffset: 20 }
    }

    Settings {
        id: customizationStore
        category: "ParallelFinder/customization"
        property color accent: "#D97757"
        property color sage: "#7C9885"
        property string fontFamily: "Segoe UI"
        property bool reducedMotion: false
        property real surfaceOpacity: 1.0
        property real radiusScale: 1.0
        property real auraOpacity: 0.20
        property real auraTrail: 0.34
        property string language: "ru"
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
    FileDialog { id: modelDialog; title: L10n.t("settings.modelPath"); fileMode: FileDialog.OpenFile; nameFilters: ["ONNX (*.onnx)", L10n.t("dialog.allFiles")]; onAccepted: Analysis.setModelPath(selectedFile.toLocalFile()) }
    FolderDialog { id: cacheDialog; title: L10n.t("settings.cachePath"); onAccepted: Analysis.setCachePath(selectedFolder.toLocalFile()) }
    FileDialog { id: exportThemeDialog; title: L10n.t("settings.exportTheme"); fileMode: FileDialog.SaveFile; nameFilters: ["Parallel Finder Theme (*.pftheme)"]; currentFile: "parallel-theme.pftheme"; onAccepted: root.themeStatus = Analysis.exportTheme(selectedFile.toLocalFile(), ({ accent: String(Theme.accent), sage: String(Theme.sage), fontFamily: Theme.fontFamily, reducedMotion: Theme.reducedMotion, surfaceOpacity: Theme.surfaceOpacity, radiusScale: Theme.radiusScale, auraOpacity: Theme.auraOpacity, auraTrail: Theme.auraTrail })) ? L10n.t("settings.profileSaveSuccess") : L10n.t("settings.profileSaveFailed") }
    FileDialog { id: importThemeDialog; title: L10n.t("settings.importTheme"); fileMode: FileDialog.OpenFile; nameFilters: ["Parallel Finder Theme (*.pftheme)", L10n.t("dialog.allFiles")]; onAccepted: root.applyTheme(Analysis.importTheme(selectedFile.toLocalFile())) }

    Component.onCompleted: {
        Theme.accent = customizationStore.accent
        Theme.sage = customizationStore.sage
        Theme.fontFamily = customizationStore.fontFamily
        Theme.reducedMotion = customizationStore.reducedMotion
        Theme.surfaceOpacity = customizationStore.surfaceOpacity
        Theme.radiusScale = customizationStore.radiusScale
        Theme.auraOpacity = customizationStore.auraOpacity
        Theme.auraTrail = customizationStore.auraTrail
        L10n.language = customizationStore.language
        root.centerInWindow()
    }
    onClosed: {
        customizationStore.accent = Theme.accent
        customizationStore.sage = Theme.sage
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.reducedMotion = Theme.reducedMotion
        customizationStore.surfaceOpacity = Theme.surfaceOpacity
        customizationStore.radiusScale = Theme.radiusScale
        customizationStore.auraOpacity = Theme.auraOpacity
        customizationStore.auraTrail = Theme.auraTrail
        customizationStore.language = L10n.language
    }
    Keys.onEscapePressed: root.close()

    function resetAppearance() {
        Theme.accent = "#D97757"
        Theme.sage = "#7C9885"
        Theme.fontFamily = "Segoe UI"
        Theme.reducedMotion = false
        Theme.surfaceOpacity = 1.0
        Theme.radiusScale = 1.0
        Theme.auraOpacity = 0.20
        Theme.auraTrail = 0.34
        customizationStore.accent = Theme.accent
        customizationStore.sage = Theme.sage
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.reducedMotion = Theme.reducedMotion
        customizationStore.surfaceOpacity = Theme.surfaceOpacity
        customizationStore.radiusScale = Theme.radiusScale
        customizationStore.auraOpacity = Theme.auraOpacity
        customizationStore.auraTrail = Theme.auraTrail
    }
    function applyTheme(theme) {
        if (!theme || !theme.accent || !theme.sage) { root.themeStatus = L10n.t("settings.profileNotRecognized"); return }
        Theme.accent = String(theme.accent)
        Theme.sage = String(theme.sage)
        if (theme.fontFamily) Theme.fontFamily = String(theme.fontFamily)
        if (theme.reducedMotion !== undefined) Theme.reducedMotion = Boolean(theme.reducedMotion)
        if (theme.surfaceOpacity !== undefined) Theme.surfaceOpacity = Number(theme.surfaceOpacity)
        if (theme.radiusScale !== undefined) Theme.radiusScale = Number(theme.radiusScale)
        if (theme.auraOpacity !== undefined) Theme.auraOpacity = Number(theme.auraOpacity)
        if (theme.auraTrail !== undefined) Theme.auraTrail = Number(theme.auraTrail)
        customizationStore.accent = Theme.accent
        customizationStore.sage = Theme.sage
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.reducedMotion = Theme.reducedMotion
        customizationStore.surfaceOpacity = Theme.surfaceOpacity
        customizationStore.radiusScale = Theme.radiusScale
        customizationStore.auraOpacity = Theme.auraOpacity
        customizationStore.auraTrail = Theme.auraTrail
        root.themeStatus = L10n.t("settings.profileApplied")
    }

    function centerInWindow() {
        if (!rootWindow || root.userPositioned) return
        root.x = Math.round((rootWindow.width - root.width) / 2)
        root.y = Math.round((rootWindow.height - root.height) / 2)
    }

    onOpened: centerInWindow()

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
            MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.rightMargin: 58; property real pressX; property real pressY; property real startX; property real startY
                cursorShape: Qt.SizeAllCursor
                onPressed: { pressX = mouse.x; pressY = mouse.y; startX = root.x; startY = root.y }
                onPositionChanged: if (pressed && rootWindow) { root.userPositioned = true; root.x = Math.max(12, Math.min(rootWindow.width - root.width - 12, startX + mouse.x - pressX)); root.y = Math.max(12, Math.min(rootWindow.height - root.height - 12, startY + mouse.y - pressY)) }
            }
        }
        TabBar {
            id: tabs
            width: parent.width
            height: 48
            background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.hairline }
            TabButton {
                id: analysisTab
                text: L10n.t("settings.tabAnalysis")
                Accessible.name: text
                contentItem: Text { text: analysisTab.text; color: analysisTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: analysisTab.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: analysisTab.checked ? Theme.panelAlt : "transparent"; border.color: analysisTab.checked ? Theme.accent : "transparent"; border.width: analysisTab.checked ? 1 : 0; radius: Theme.radiusButton }
            }
            TabButton {
                id: customizationTab
                text: L10n.t("settings.tabAppearance")
                Accessible.name: text
                contentItem: Text { text: customizationTab.text; color: customizationTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: customizationTab.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: customizationTab.checked ? Theme.panelAlt : "transparent"; border.color: customizationTab.checked ? Theme.accent : "transparent"; border.width: customizationTab.checked ? 1 : 0; radius: Theme.radiusButton }
            }
            TabButton {
                id: keyboardTab
                text: L10n.t("settings.tabKeyboard")
                Accessible.name: text
                contentItem: Text { text: keyboardTab.text; color: keyboardTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: keyboardTab.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: keyboardTab.checked ? Theme.panelAlt : "transparent"; border.color: keyboardTab.checked ? Theme.accent : "transparent"; border.width: keyboardTab.checked ? 1 : 0; radius: Theme.radiusButton }
            }
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
                            PfComboBox { id: provider; width: 190; model: ["Auto", "TensorRT", "CUDA", "DirectML", "CPU"]; currentIndex: ["auto", "tensorrt", "cuda", "dml", "cpu"].indexOf(Analysis.providerChoice); Accessible.name: L10n.t("settings.provider"); onActivated: Analysis.providerChoice = ["auto", "tensorrt", "cuda", "dml", "cpu"][currentIndex] }
                            Text { text: AppInfo.gpuSummary; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; width: parent.width - 345 }
                        }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.language"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            PfComboBox { width: 190; model: [L10n.t("settings.languageRussian"), L10n.t("settings.languageEnglish")]; currentIndex: L10n.language === "en" ? 1 : 0; Accessible.name: L10n.t("settings.language"); onActivated: { L10n.language = currentIndex === 1 ? "en" : "ru"; customizationStore.language = L10n.language } }
                        }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.modelPath"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            PfTextField { width: parent.width - 230; text: Analysis.modelPath; placeholderText: L10n.t("settings.modelPlaceholder"); Accessible.name: L10n.t("settings.modelPath"); onEditingFinished: Analysis.setModelPath(text) }
                            PfButton { width: 80; text: L10n.t("settings.browse"); quiet: true; onClicked: modelDialog.open() }
                        }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.cachePath"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            PfTextField { width: parent.width - 230; text: Analysis.cachePath; placeholderText: L10n.t("settings.cachePlaceholder"); Accessible.name: L10n.t("settings.cachePath"); onEditingFinished: Analysis.setCachePath(text) }
                            PfButton { width: 80; text: L10n.t("settings.browse"); quiet: true; onClicked: cacheDialog.open() }
                        }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.cacheLimit"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            PfSlider { width: parent.width - 240; from: 0.25; to: 128; stepSize: 0.25; value: Analysis.cacheLimitGb; Accessible.name: L10n.t("settings.cacheLimit"); onMoved: Analysis.cacheLimitGb = value }
                            Text { width: 80; text: Analysis.cacheLimitGb.toFixed(2); color: Theme.accent; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.scene"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 34; spacing: 12
                            Text { width: 135; text: L10n.t("settings.sceneThreshold"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                            PfSlider { width: parent.width - 240; from: 1; to: 255; stepSize: 1; value: Analysis.sceneThreshold; Accessible.name: L10n.t("settings.sceneThreshold"); onMoved: Analysis.sceneThreshold = value }
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
                        PfComboBox { width: parent.width; model: ["Segoe UI", "Arial", "Verdana"]; currentIndex: model.indexOf(Theme.fontFamily); Accessible.name: L10n.t("settings.font"); onActivated: { Theme.fontFamily = currentText; customizationStore.fontFamily = currentText } }
                        Row { width: parent.width; height: 32; spacing: 12
                            Text { width: 170; text: L10n.t("settings.surfaceOpacity"); color: Theme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            PfSlider { width: parent.width - 240; from: 0.72; to: 1.0; stepSize: 0.01; value: Theme.surfaceOpacity; Accessible.name: L10n.t("settings.surfaceOpacity"); onMoved: { Theme.surfaceOpacity = value; customizationStore.surfaceOpacity = value } }
                            Text { width: 58; text: Math.round(Theme.surfaceOpacity * 100) + "%"; color: Theme.accent; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        }
                        Row { width: parent.width; height: 32; spacing: 12
                            Text { width: 170; text: L10n.t("settings.radiusScale"); color: Theme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            PfSlider { width: parent.width - 240; from: 0.7; to: 1.35; stepSize: 0.05; value: Theme.radiusScale; Accessible.name: L10n.t("settings.radiusScale"); onMoved: { Theme.radiusScale = value; customizationStore.radiusScale = value } }
                            Text { width: 58; text: Theme.radiusScale.toFixed(2) + "×"; color: Theme.accent; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        }
                        Text { text: L10n.t("settings.motionField"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Row { width: parent.width; height: 32; spacing: 12
                            Text { width: 170; text: L10n.t("settings.auraOpacity"); color: Theme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            PfSlider { width: parent.width - 240; from: 0.04; to: 0.42; stepSize: 0.01; value: Theme.auraOpacity; Accessible.name: L10n.t("settings.auraOpacity"); onMoved: { Theme.auraOpacity = value; customizationStore.auraOpacity = value } }
                            Text { width: 58; text: Math.round(Theme.auraOpacity * 100) + "%"; color: Theme.accent; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        }
                        Row { width: parent.width; height: 32; spacing: 12
                            Text { width: 170; text: L10n.t("settings.auraTrail"); color: Theme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            PfSlider { width: parent.width - 240; from: 0.12; to: 0.58; stepSize: 0.01; value: Theme.auraTrail; Accessible.name: L10n.t("settings.auraTrail"); onMoved: { Theme.auraTrail = value; customizationStore.auraTrail = value } }
                            Text { width: 58; text: Math.round(Theme.auraTrail * 100) + "%"; color: Theme.accent; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        }
                        PfCheckBox { text: L10n.t("settings.reducedMotion"); checked: Theme.reducedMotion; onToggled: { Theme.reducedMotion = checked; customizationStore.reducedMotion = checked } }
                        Row { width: parent.width; spacing: 8
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.resetAppearance"); quiet: true; onClicked: root.resetAppearance() }
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.exportTheme"); quiet: true; onClicked: exportThemeDialog.open() }
                        }
                        Row { width: parent.width; spacing: 8
                            PfButton { width: (parent.width - 8) / 2; text: L10n.t("settings.importTheme"); quiet: true; onClicked: importThemeDialog.open() }
                            Text { width: (parent.width - 8) / 2; text: root.themeStatus || L10n.t("settings.profileSaved"); color: root.themeStatus ? Theme.accent : Theme.textDisabled; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
                        }
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
