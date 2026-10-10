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
    objectName: "settingsDialog"
    property bool backdropClosing: false
    onAboutToShow: backdropClosing = false
    onAboutToHide: { backdropClosing = true; accentPicker.close() }
    property real entranceOffset: 0
    property var rootWindow
    property bool userPositioned: false
    property string themeStatus: ""
    readonly property var installedFonts: Qt.fontFamilies()
    readonly property var fontChoices: {
        const installed = root.installedFonts
        const preferred = ["Segoe UI Variable", "Segoe UI", "Inter", "Montserrat"]
        const available = preferred.filter(function(family) { return installed.indexOf(family) >= 0 })
        if (!available.length) available.push(Qt.application.font.family)
        return available
    }
    readonly property var providerIds: ["auto", "dml", "cuda", "tensorrt", "cpu"]
    readonly property var providerLabels: [
        L10n.t("settings.providerAuto"),
        L10n.t("settings.providerDirectMl"),
        L10n.t("settings.providerCuda"),
        L10n.t("settings.providerTensorRt"),
        L10n.t("settings.providerCpu")
    ]
    readonly property var accentIds: ["orange", "blue", "violet", "teal"]
    readonly property var accentLabels: [
        L10n.t("settings.accentOrange"),
        L10n.t("settings.accentBlue"),
        L10n.t("settings.accentViolet"),
        L10n.t("settings.accentTeal"),
        L10n.language === "ru" ? "Свой цвет" : "Custom color"
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
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Theme.motionRevealDuration; easing.type: Easing.OutQuint }
            NumberAnimation { target: root; property: "entranceOffset"; from: 28; to: 0; duration: Theme.motionRevealDuration; easing.type: Easing.OutQuint }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
        }
    }
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: GraphicsInfo.api === GraphicsInfo.Software ? "#99000000" : "transparent" }
    background: Rectangle {
        transform: Translate { y: root.entranceOffset }
        color: Theme.heroPanel
        radius: Theme.radiusOverlay
        border.color: Theme.hairline
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowOverlay; shadowOpacity: 0.78; shadowBlur: 1.0; shadowHorizontalOffset: 0; shadowVerticalOffset: 20 }
    }

    QtObject {
        id: customizationStore
        property bool loaded: false
        property string language: "en"
        property string fontFamily: "Segoe UI Variable"
        property string customFontPath: ""
        property real surfaceOpacity: 1.0
        property string accentColor: "orange"
        property bool reducedMotion: false
        function persist() {
            if (!loaded) return
            if (!AppInfo.savePreferences(Object.assign({}, AppInfo.loadPreferences(), {language: language, fontFamily: fontFamily,
                customFontPath: customFontPath, surfaceOpacity: surfaceOpacity,
                accentColor: accentColor, reducedMotion: reducedMotion})))
                root.themeStatus = L10n.t("settings.saveFailed")
        }
        onLanguageChanged: persist()
        onFontFamilyChanged: persist()
        onCustomFontPathChanged: persist()
        onSurfaceOpacityChanged: persist()
        onAccentColorChanged: persist()
        onReducedMotionChanged: persist()
    }
    FontLoader {
        id: customFont
        source: customizationStore.customFontPath
        onStatusChanged: if (status === FontLoader.Error) {
            root.themeStatus = L10n.t("settings.fontLoadFailed")
        } else if (status === FontLoader.Ready) {
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
            customizationStore.customFontPath = String(selectedFile)
        }
    }
    FolderDialog {
        id: cacheDialog
        title: L10n.t("settings.chooseCache")
        onAccepted: Analysis.setCachePath(String(selectedFolder))
    }

    Component.onCompleted: {
        const saved = AppInfo.loadPreferences()
        customizationStore.language = saved.language === "ru" ? "ru" : "en"
        if (typeof saved.fontFamily === "string") customizationStore.fontFamily = saved.fontFamily
        if (typeof saved.customFontPath === "string") customizationStore.customFontPath = saved.customFontPath
        if (typeof saved.surfaceOpacity === "number") customizationStore.surfaceOpacity = Math.max(0.25, Math.min(1, saved.surfaceOpacity))
        if (typeof saved.accentColor === "string") customizationStore.accentColor = saved.accentColor
        if (typeof saved.reducedMotion === "boolean") customizationStore.reducedMotion = saved.reducedMotion
        L10n.language = customizationStore.language
        Theme.reducedMotion = customizationStore.reducedMotion
        Theme.surfaceOpacity = customizationStore.surfaceOpacity
        root.applyAccent(customizationStore.accentColor)
        if (customFont.status !== FontLoader.Ready) {
            Theme.fontFamily = root.fontChoices[0]
            if (!customizationStore.customFontPath.length) root.applyFont(customizationStore.fontFamily)
        }
        root.centerInWindow()
        customizationStore.loaded = true
        customizationStore.persist()
    }
    onClosed: {
        customizationStore.language = L10n.language
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.surfaceOpacity = Theme.surfaceOpacity
    }
    onOpened: {
        centerInWindow()
        AppInfo.rescanProviders()
        appearanceAccentChoice.currentIndex = root.accentIds.indexOf(customizationStore.accentColor) >= 0 ? root.accentIds.indexOf(customizationStore.accentColor) : 4
        // Prevent the close icon or first tab from receiving an orange focus
        // ring just because the popup was opened with the mouse.
        Qt.callLater(function() { root.forceActiveFocus() })
    }
    Connections {
        target: AppInfo
        function onPreferencesSaveFailed(error) { root.themeStatus = L10n.t("settings.saveFailed") + ": " + error }
        function onBackendInitializationChanged() {
            if (root.visible && !AppInfo.backendInitializing) AppInfo.rescanProviders()
        }
    }

    function centerInWindow() {
        if (!rootWindow || root.userPositioned) return
        root.x = Math.round((rootWindow.width - root.width) / 2)
        root.y = Math.round((rootWindow.height - root.height) / 2)
    }
    function resetAppearance() {
        customizationStore.customFontPath = ""
        Theme.fontFamily = root.fontChoices[0]
        Theme.surfaceOpacity = 1.0
        Theme.reducedMotion = false
        customizationStore.reducedMotion = false
        root.applyAccent("orange")
        customizationStore.fontFamily = Theme.fontFamily
        customizationStore.customFontPath = ""
        customizationStore.surfaceOpacity = Theme.surfaceOpacity
        root.themeStatus = L10n.t("settings.appearanceResetDone")
    }
    function applyFont(family) {
        if (root.installedFonts.indexOf(family) < 0) {
            root.themeStatus = L10n.t("settings.fontUnavailable") + ": " + family
            return false
        }
        customizationStore.customFontPath = ""
        Theme.fontFamily = family
        customizationStore.fontFamily = family
        root.themeStatus = ""
        return true
    }
    function accentValue(id) {
        if (/^#[0-9a-fA-F]{6}$/.test(String(id))) return id
        if (id === "blue") return "#5D8DDE"
        if (id === "violet") return "#A679D6"
        if (id === "teal") return "#54B7A5"
        return "#D97757"
    }
    function applyAccent(id) {
        const normalized = root.accentIds.indexOf(id) >= 0 ? id : /^#[0-9a-fA-F]{6}$/.test(String(id)) ? String(id).toUpperCase() : "orange"
        Theme.accent = root.accentValue(normalized)
        customizationStore.accentColor = normalized
        appearanceAccentChoice.currentIndex = root.accentIds.indexOf(normalized) >= 0 ? root.accentIds.indexOf(normalized) : 4
    }
    function applyCustomAccent(value) {
        if (!/^#[0-9a-fA-F]{6}$/.test(String(value))) return false
        applyAccent(value)
        return true
    }
    Popup {
        id: accentPicker
        objectName: "accentColorPicker"
        parent: Overlay.overlay
        padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusButton; border.color: Theme.hairlineStrong }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionDuration } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionDuration } }
        contentItem: Column {
            width: 246
            spacing: 10
            Row {
            spacing: 6
            Repeater {
                model: ["#D97757", "#5D8DDE", "#A679D6", "#54B7A5"]
                delegate: Button {
                    required property int index
                    required property string modelData
                    objectName: "accentPaletteColor" + index
                    width: 28; height: 28
                    Accessible.name: modelData
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    background: Rectangle { radius: 6; color: modelData || Theme.well; border.color: parent.hovered ? Theme.accent : Theme.hairlineStrong; border.width: 1 }
                    contentItem: Text { text: ""; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: { root.applyAccent(root.accentIds[index]); hexField.text = String(Theme.accent).toUpperCase(); hueSlider.value = Theme.accent.hsvHue; colorPlane.saturation = Theme.accent.hsvSaturation; colorPlane.brightness = Theme.accent.hsvValue }
                }
            }
            }
            Rectangle {
                id: colorPlane
                width: parent.width; height: 112; radius: 5
                color: Qt.hsva(hueSlider.value, 1, 1, 1)
                Rectangle { anchors.fill: parent; radius: 5; gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "white" } GradientStop { position: 1; color: "transparent" } } }
                Rectangle { anchors.fill: parent; radius: 5; gradient: Gradient { GradientStop { position: 0; color: "transparent" } GradientStop { position: 1; color: "black" } } }
                property real saturation: 0.7
                property real brightness: 0.85
                Rectangle { x: colorPlane.saturation * (parent.width - 10); y: (1 - colorPlane.brightness) * (parent.height - 10); width: 10; height: 10; radius: 5; color: "transparent"; border.color: "white"; border.width: 2 }
                MouseArea {
                    anchors.fill: parent
                    function pick(mouse) {
                        colorPlane.saturation = Math.max(0, Math.min(1, mouse.x / width))
                        colorPlane.brightness = Math.max(0, Math.min(1, 1 - mouse.y / height))
                        hexField.text = String(Qt.hsva(hueSlider.value, colorPlane.saturation, colorPlane.brightness, 1)).toUpperCase()
                        root.applyCustomAccent(hexField.text)
                    }
                    onPressed: function(mouse) { pick(mouse) }
                    onPositionChanged: function(mouse) { if (pressed) pick(mouse) }
                }
            }
            Slider {
                id: hueSlider; objectName: "accentPaletteHue"; width: parent.width; from: 0; to: 1; value: 0.05
                Accessible.name: L10n.language === "ru" ? "Оттенок" : "Hue"
                handle: Rectangle {
                    x: hueSlider.leftPadding + hueSlider.visualPosition * (hueSlider.availableWidth - width)
                    y: (hueSlider.height - height) / 2; width: 16; height: 16; radius: 8
                    color: Theme.textPrimary; border.color: Theme.heroPanel; border.width: 2
                }
                onMoved: { hexField.text = String(Qt.hsva(value, colorPlane.saturation, colorPlane.brightness, 1)).toUpperCase(); root.applyCustomAccent(hexField.text) }
                background: Rectangle {
                    x: hueSlider.leftPadding; y: (hueSlider.height - height) / 2; width: hueSlider.availableWidth; height: 8; radius: 4
                    gradient: Gradient { orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "#ff0000" } GradientStop { position: 0.167; color: "#ffff00" }
                        GradientStop { position: 0.333; color: "#00ff00" } GradientStop { position: 0.5; color: "#00ffff" }
                        GradientStop { position: 0.667; color: "#0000ff" } GradientStop { position: 0.833; color: "#ff00ff" } GradientStop { position: 1; color: "#ff0000" }
                    }
                }
            }
            Row {
                spacing: 8
                TextField {
                    id: hexField; objectName: "accentPaletteHex"; width: 126; height: 32
                    text: "#D56565"; maximumLength: 7; color: Theme.textPrimary; font.family: Theme.monoFont
                    Accessible.name: "HEX"
                    background: Rectangle { radius: 5; color: Theme.well; border.color: hexField.acceptableInput ? Theme.hairlineStrong : Theme.accent }
                    validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                    onTextEdited: if (root.applyCustomAccent(text)) root.refreshAccentPalette()
                    onAccepted: if (root.applyCustomAccent(text)) accentPicker.close()
                }
                PfButton { objectName: "accentPaletteApply"; width: 112; height: 32; compact: true; text: Review.t("apply"); enabled: hexField.acceptableInput; onClicked: if (root.applyCustomAccent(hexField.text)) accentPicker.close() }
            }
        }
        onAboutToShow: root.refreshAccentPalette()
    }
    function refreshAccentPalette() {
        hexField.text = String(Theme.accent).toUpperCase()
        hueSlider.value = Theme.accent.hsvHue < 0 ? 0 : Theme.accent.hsvHue
        colorPlane.saturation = Theme.accent.hsvSaturation; colorPlane.brightness = Theme.accent.hsvValue
    }
    function openAccentPalette(item) {
        const point = item.mapToItem(Overlay.overlay, 0, item.height)
        accentPicker.x = Math.max(8, Math.min(point.x, Overlay.overlay.width - accentPicker.width - 8))
        accentPicker.y = Math.max(8, Math.min(point.y, Overlay.overlay.height - accentPicker.height - 8))
        accentPicker.open()
    }
    function providerStatusText(id) {
        const revision = AppInfo.providersRevision
        if (AppInfo.backendInitializing) return L10n.t("settings.providerChecking")
        const key = String(id || "auto").toLowerCase()
        if (key === "auto") return L10n.t("settings.providerAutoHint")
        if (AppInfo.backendAvailable(key)) return "✓ " + L10n.t("settings.providerReady")
        if (AppInfo.providersScanning) return L10n.t("settings.providerChecking")
        const installation = AppInfo.providerInstallation(key)
        if (installation.available) return L10n.t("settings.providerValidatedRestart")
        if (installation.installed) return L10n.t("settings.providerInvalid") + ": " + installation.reason
        const reason = String(AppInfo.backendReason(key) || "").toLowerCase()
        if (key === "cuda" && (reason.indexOf("cuda") >= 0 || reason.indexOf("onnxruntime") >= 0)) return L10n.t("settings.providerCudaMissing")
        if (key === "tensorrt" && (reason.indexOf("tensorrt") >= 0 || reason.indexOf("onnxruntime") >= 0)) return L10n.t("settings.providerTensorRtMissing")
        if (key === "dml" && (reason.indexOf("directml") >= 0 || reason.indexOf("dml") >= 0)) return L10n.t("settings.providerDmlMissing")
        return L10n.t("settings.providerUnavailable")
    }
    function providerDownloadText(raw) {
        const status = String(raw || "")
        if (AppInfo.providerDownloadState === "installed") return L10n.t("settings.providerRestart")
        if (AppInfo.providerDownloadState === "checking") return L10n.t("settings.providerChecking")
        if (AppInfo.providerDownloadState === "downloading") return L10n.t("settings.providerDownloading") + " " + Math.round(AppInfo.providerDownloadProgress * 100) + "%"
        if (!status.length) return L10n.t("settings.providerDownloadHint")
        const lower = status.toLowerCase()
        if (lower.indexOf("404") >= 0 || lower.indexOf("providers.json") >= 0) return L10n.t("settings.providerManifestMissing")
        if (lower.indexOf("internet") >= 0 || lower.indexOf("network") >= 0 || lower.indexOf("transfer") >= 0) return L10n.t("settings.providerNetworkError")
        if (lower.indexOf("downloaded") >= 0 || lower.indexOf("готов") >= 0) return L10n.t("settings.providerDownloadDone")
        return status
    }

    contentItem: Item {
        transform: Translate { y: root.entranceOffset }
        clip: true
        layer.enabled: root.visible && GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect { maskEnabled: true; maskSource: roundedMask }
        Rectangle { id: roundedMask; anchors.fill: parent; radius: Theme.radiusOverlay; color: "white"; visible: false; layer.enabled: true }
        Column {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            width: parent.width
            height: 58
            radius: GraphicsInfo.api === GraphicsInfo.Software ? Theme.radiusOverlay : 0
            color: Theme.surfaceRaised
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
            Text { anchors.left: parent.left; anchors.leftMargin: 22; anchors.verticalCenter: parent.verticalCenter; text: L10n.t("settings.windowTitle"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 13; font.weight: Font.DemiBold }
            PfIconButton { objectName: "settingsCloseButton"; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"; accessibleName: L10n.t("common.close"); activeFocusOnTab: true; focusPolicy: Qt.StrongFocus; onClicked: root.close() }
            MouseArea {
                objectName: "settingsDragArea"
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.rightMargin: 56
                property real pressSceneX
                property real pressSceneY
                property real startX
                property real startY
                cursorShape: Qt.SizeAllCursor
                onPressed: function(mouse) {
                    const scenePoint = mapToItem(rootWindow.contentItem, mouse.x, mouse.y)
                    pressSceneX = scenePoint.x
                    pressSceneY = scenePoint.y
                    startX = root.x
                    startY = root.y
                    root.userPositioned = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !rootWindow) return
                    // mouse.x/y are local to an item that itself moves with the
                    // popup. Using them directly creates a feedback loop and
                    // makes the window oscillate while dragging. Convert to the
                    // stable root-window coordinate system first.
                    const scenePoint = mapToItem(rootWindow.contentItem, mouse.x, mouse.y)
                    root.x = Math.max(12, Math.min(rootWindow.width - root.width - 12,
                                                   startX + scenePoint.x - pressSceneX))
                    root.y = Math.max(12, Math.min(rootWindow.height - root.height - 12,
                                                   startY + scenePoint.y - pressSceneY))
                }
            }
        }
        TabBar {
            id: tabs
            objectName: "settingsTabs"
            x: 0
            width: parent.width
            height: 44
            padding: 4
            spacing: 0
            background: Rectangle {
        transform: Translate { y: root.entranceOffset }
                color: Theme.well
                radius: Theme.radiusButton + 4
                border.color: Theme.hairline
                Rectangle {
                    objectName: "settingsTabIndicator"
                    y: 4
                    x: 4 + tabs.currentIndex * (tabs.width - 8) / 3
                    width: (tabs.width - 8) / 3; height: tabs.height - 8
                    radius: Theme.radiusButton
                    color: Theme.surfaceMuted
                    border.color: Theme.hairlineStrong
                    Behavior on x { NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.OutQuint } }
                }
            }
            TabButton {
                id: analysisTab
                objectName: "settingsAnalysisTab"
                width: (tabs.width - 8) / 3
                height: tabs.height - 8
                implicitHeight: tabs.height - 8
                padding: 0
                text: L10n.t("settings.tabAnalysis")
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                contentItem: Text { text: analysisTab.text; color: analysisTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: analysisTab.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Item {}
            }
            TabButton {
                id: customizationTab
                objectName: "settingsAppearanceTab"
                width: (tabs.width - 8) / 3
                height: tabs.height - 8
                implicitHeight: tabs.height - 8
                padding: 0
                text: L10n.t("settings.tabAppearance")
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                contentItem: Text { text: customizationTab.text; color: customizationTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: customizationTab.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Item {}
            }
            TabButton {
                id: hotkeysTab
                objectName: "settingsHotkeysTab"
                width: (tabs.width - 8) / 3; height: tabs.height - 8
                padding: 0; text: Review.t("hotkeys")
                contentItem: Text { text: hotkeysTab.text; color: hotkeysTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Item {}
            }
        }
        StackLayout { id: pages; width: parent.width; height: parent.height - 102; currentIndex: tabs.currentIndex
            PfReveal {
                active: root.visible && tabs.currentIndex === 0
                delay: 75
                distance: 24
                Flickable { objectName: "settingsAnalysisFlick"; anchors.fill: parent; anchors.margins: 22; clip: true; contentWidth: width; contentHeight: analysisBody.implicitHeight + 30; boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout { id: analysisBody; width: parent.width; spacing: 14
                        Text { text: L10n.t("settings.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.subtitle"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 13; wrapMode: Text.WordWrap }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("updates.title"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfComboBox { objectName: "updateChannel"; Layout.fillWidth: true; model: ["Stable", "Beta"]; currentIndex: Updates.channel === "beta" ? 1 : 0; enabled: !Updates.busy; Accessible.name: L10n.t("updates.channel"); onActivated: Updates.channel = currentIndex === 1 ? "beta" : "stable" }
                        PfCheckBox { objectName: "automaticUpdateCheck"; Layout.fillWidth: true; text: L10n.t("updates.autoCheck"); checked: Updates.automaticCheck; onToggled: Updates.automaticCheck = checked }
                        PfCheckBox { objectName: "automaticUpdateDownload"; Layout.fillWidth: true; text: L10n.t("updates.autoDownload"); checked: Updates.automaticDownload; onToggled: Updates.automaticDownload = checked }
                        PfButton { Layout.fillWidth: true; text: L10n.t("updates.check"); enabled: !Updates.busy; quiet: true; onClicked: { root.close(); Updates.check() } }
                        Text { Layout.fillWidth: true; text: L10n.t("updates.consent"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.environment"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.gpuSetupSteps"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        RowLayout { Layout.fillWidth: true; spacing: 12
                            Text { Layout.preferredWidth: 128; Layout.minimumWidth: 0; text: L10n.t("settings.provider"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter; ToolTip.visible: providerHelp.hovered; ToolTip.text: L10n.t("settings.providerHint"); ToolTip.delay: 350; elide: Text.ElideRight }
                            HoverHandler { id: providerHelp }
                            PfComboBox {
                                id: provider
                                Layout.preferredWidth: 180
                                Layout.minimumWidth: 0
                                Layout.fillWidth: true
                                model: root.providerLabels
                                Accessible.name: L10n.t("settings.provider")
                                onActivated: Analysis.providerChoice = root.providerIds[currentIndex]
                                // ComboBox may update currentIndex internally when its model
                                // is rebuilt (language/theme changes). Re-assert the persisted
                                // backend instead of silently displaying Auto.
                                Binding {
                                    target: provider
                                    property: "currentIndex"
                                    value: Math.max(0, root.providerIds.indexOf(Analysis.providerChoice))
                                    restoreMode: Binding.RestoreBindingOrValue
                                }
                            }
                        }
                        Text { Layout.fillWidth: true; text: AppInfo.gpuSummary; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        Text {
                            Layout.fillWidth: true
                            text: Analysis.providerChoice === "auto"
                                ? L10n.t("settings.providerAutoHint")
                                : root.providerStatusText(Analysis.providerChoice)
                            color: Analysis.providerChoice === "auto" || AppInfo.backendAvailable(Analysis.providerChoice) ? Theme.sage : Theme.accent
                            font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: Analysis.providerChoice !== "auto"
                                && Analysis.providerChoice !== "cpu"
                                && !AppInfo.backendAvailable(Analysis.providerChoice)
                            PfButton {
                                Layout.preferredWidth: 154
                                Layout.minimumWidth: 0
                                compact: true
                                text: AppInfo.providerDownloading ? L10n.t("settings.providerDownloading") : L10n.t("settings.providerDownload")
                                enabled: !AppInfo.backendInitializing && !AppInfo.providerDownloading && !AppInfo.providersScanning
                                onClicked: AppInfo.downloadProvider(Analysis.providerChoice)
                            }
                            PfButton {
                                Layout.preferredWidth: 136
                                Layout.minimumWidth: 0
                                compact: true
                                quiet: true
                                text: L10n.t("settings.providerGuide")
                                onClicked: Qt.openUrlExternally(AppInfo.providerGuideUrl(Analysis.providerChoice))
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.providerDownloadText(AppInfo.providerDownloadStatus)
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }
                        PfButton { Layout.fillWidth: true; compact: true; text: L10n.t("settings.providerRescan"); enabled: !AppInfo.backendInitializing && !AppInfo.providersScanning && !AppInfo.providerDownloading; onClicked: AppInfo.rescanProviders() }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.cache"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        RowLayout { Layout.fillWidth: true; spacing: 8
                            PfTextField { Layout.fillWidth: true; font.family: Theme.monoFont; text: Analysis.cachePath; placeholderText: L10n.t("settings.cachePlaceholder"); Accessible.name: L10n.t("settings.cachePath"); onEditingFinished: Analysis.setCachePath(text) }
                            PfButton { Layout.preferredWidth: 96; compact: true; text: L10n.t("settings.choose"); quiet: true; onClicked: cacheDialog.open() }
                        }
                        PfSliderField { Layout.fillWidth: true; label: L10n.t("settings.cacheLimit"); from: 0.25; to: 128; stepSize: 0.25; value: Analysis.cacheLimitGb; decimals: 2; suffix: "GB"; tooltipText: L10n.t("settings.cacheLimitHint"); Accessible.name: L10n.t("settings.cacheLimit"); onValueEdited: Analysis.setCacheLimitGb(nextValue) }
                        PfSliderField { Layout.fillWidth: true; label: L10n.t("settings.processingThreads"); from: 0; to: 64; stepSize: 1; value: Analysis.processingThreads; decimals: 0; integer: true; tooltipText: L10n.t("settings.processingThreadsHint"); Accessible.name: L10n.t("settings.processingThreads"); onValueEdited: Analysis.setProcessingThreads(Math.round(nextValue)) }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.systemHint"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                    }
                }
            }
            PfReveal {
                active: root.visible && tabs.currentIndex === 1
                delay: 75
                distance: 24
                PfButton { anchors.left: parent.left; anchors.leftMargin: 22; anchors.bottom: parent.bottom; anchors.bottomMargin: 16; width: 210; compact: true; text: L10n.t("settings.resetAppearance"); quiet: true; onClicked: root.resetAppearance() }
                Flickable { anchors.fill: parent; anchors.margins: 22; anchors.bottomMargin: 66; clip: true; contentWidth: width; contentHeight: appearanceBody.implicitHeight + 30; boundsBehavior: Flickable.StopAtBounds
                    Column { id: appearanceBody; width: parent.width; spacing: 14
                        Text { text: L10n.t("settings.appearanceTitle"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                        Text { text: L10n.t("settings.appearanceSubtitle"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        Text { text: L10n.t("settings.language"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfComboBox { width: parent.width; model: [L10n.t("settings.languageRussian"), L10n.t("settings.languageEnglish")]; currentIndex: L10n.language === "en" ? 1 : 0; Accessible.name: L10n.t("settings.language"); onActivated: { L10n.language = currentIndex === 1 ? "en" : "ru"; customizationStore.language = L10n.language } }
                        Text { text: L10n.t("settings.font"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        RowLayout { width: parent.width; spacing: 8
                            PfComboBox { objectName: "appearanceFontChoice"; Layout.fillWidth: true; model: root.fontChoices; currentIndex: model.indexOf(Theme.fontFamily); Accessible.name: L10n.t("settings.font"); onActivated: root.applyFont(currentText) }
                            PfButton { Layout.preferredWidth: 132; text: L10n.t("settings.fontAdd"); quiet: true; onClicked: fontDialog.open() }
                        }
                        Text { visible: customFont.status === FontLoader.Ready; text: L10n.t("settings.fontLoaded") + ": " + customFont.name; color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
                        Text { text: L10n.t("settings.accentColor"); color: Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        RowLayout {
                            width: parent.width; spacing: 8
                            PfComboBox { id: appearanceAccentChoice; objectName: "appearanceAccentChoice"; Layout.fillWidth: true; model: root.accentLabels; currentIndex: root.accentIds.indexOf(customizationStore.accentColor) >= 0 ? root.accentIds.indexOf(customizationStore.accentColor) : 4; Accessible.name: L10n.t("settings.accentColor"); onActivated: if (currentIndex < 4) root.applyAccent(root.accentIds[currentIndex]); else root.openAccentPalette(this) }
                            PfButton { objectName: "accentPaletteButton"; compact: true; text: L10n.language === "ru" ? "Палитра" : "Palette"; onClicked: root.openAccentPalette(this) }
                        }
                        PfSliderField { width: parent.width; label: L10n.t("settings.surfaceOpacity"); from: 0.25; to: 1.0; stepSize: 0.01; value: Theme.surfaceOpacity; displayScale: 100; decimals: 0; suffix: "%"; tooltipText: L10n.t("settings.surfaceOpacityHint"); Accessible.name: L10n.t("settings.surfaceOpacity"); onValueEdited: { Theme.surfaceOpacity = nextValue; customizationStore.surfaceOpacity = nextValue } }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        PfCheckBox { text: L10n.t("settings.reducedMotion"); checked: Theme.reducedMotion; onToggled: { Theme.reducedMotion = checked; customizationStore.reducedMotion = checked } }
                        Text { width: parent.width; text: root.themeStatus || L10n.t("settings.profileSaved"); color: root.themeStatus ? Theme.accent : Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                }
            }
            PfReveal {
                active: root.visible && tabs.currentIndex === 2
                distance: 18
                HotkeysPage { anchors.fill: parent; anchors.margins: 22 }
            }
        }
        }
    }
}
