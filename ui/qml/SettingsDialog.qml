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
    readonly property bool selectedProviderInstalled: {
        const revision = AppInfo.providersRevision
        return AppInfo.providerInstalled(Analysis.providerChoice)
    }
    readonly property var installedFonts: Qt.fontFamilies()
    readonly property var fontChoices: {
        const installed = root.installedFonts
        const preferred = ["Segoe UI Variable", "Segoe UI", "Aptos", "Inter", "Arial", "Calibri", "Montserrat"]
        const available = preferred.filter(function(family) { return installed.indexOf(family) >= 0 })
        const remaining = installed.filter(function(family) {
            return family.length > 0 && family[0] !== "@" && available.indexOf(family) < 0
        }).sort(function(a, b) { return a.localeCompare(b) })
        return available.concat(remaining)
    }
    readonly property var languageIds: ["ru", "en", "ky", "kk", "uk", "tr"]
    readonly property var languageNames: ["Русский", "English", "Кыргызча", "Қазақша", "Українська", "Türkçe"]
    readonly property var providerIds: ["auto", "dml", "cuda", "tensorrt", "cpu"]
    readonly property var providerLabels: [
        L10n.t("settings.providerAuto"),
        L10n.t("settings.providerDirectMl"),
        L10n.t("settings.providerCuda"),
        L10n.t("settings.providerTensorRt"),
        L10n.t("settings.providerCpu")
    ]
    readonly property var accentIds: ["orange", "blue", "violet", "teal"]
    readonly property var gradientIds: ["sunset", "ocean", "aurora"]
    readonly property var accentLabels: [
        L10n.t("settings.accentOrange"),
        L10n.t("settings.accentBlue"),
        L10n.t("settings.accentViolet"),
        L10n.t("settings.accentTeal")
    ]
    property string accentPickerTarget: "start"
    readonly property int customAccentIndex: accentIds.length
    readonly property int customGradientIndex: gradientIds.length
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
        property string customAccentStart: "#5D8DDE"
        property string customAccentEnd: "#A679D6"
        property bool reducedMotion: false
        function persist() {
            if (!loaded) return
            if (!AppInfo.savePreferences(Object.assign({}, AppInfo.loadPreferences(), {language: language, fontFamily: fontFamily,
                customFontPath: customFontPath, surfaceOpacity: surfaceOpacity,
                accentColor: accentColor, customAccentStart: customAccentStart,
                customAccentEnd: customAccentEnd, reducedMotion: reducedMotion})))
                root.themeStatus = L10n.t("settings.saveFailed")
        }
        onLanguageChanged: persist()
        onFontFamilyChanged: persist()
        onCustomFontPathChanged: persist()
        onSurfaceOpacityChanged: persist()
        onAccentColorChanged: persist()
        onCustomAccentStartChanged: persist()
        onCustomAccentEndChanged: persist()
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
        customizationStore.language = root.languageIds.indexOf(saved.language) >= 0 ? saved.language : "en"
        if (typeof saved.fontFamily === "string") customizationStore.fontFamily = saved.fontFamily
        if (typeof saved.customFontPath === "string") customizationStore.customFontPath = saved.customFontPath
        if (typeof saved.surfaceOpacity === "number") customizationStore.surfaceOpacity = Math.max(0.25, Math.min(1, saved.surfaceOpacity))
        if (typeof saved.accentColor === "string") customizationStore.accentColor = saved.accentColor
        if (/^#[0-9a-fA-F]{6}$/.test(String(saved.customAccentStart || ""))) customizationStore.customAccentStart = String(saved.customAccentStart).toUpperCase()
        if (/^#[0-9a-fA-F]{6}$/.test(String(saved.customAccentEnd || ""))) customizationStore.customAccentEnd = String(saved.customAccentEnd).toUpperCase()
        if (typeof saved.reducedMotion === "boolean") customizationStore.reducedMotion = saved.reducedMotion
        if (typeof saved.darkMode === "boolean") Theme.darkMode = saved.darkMode
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
        appearanceAccentChoice.currentIndex = root.selectedAccentIndex()
        appearanceGradientChoice.currentIndex = root.selectedGradientIndex()
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
    Connections {
        target: root.rootWindow
        function onWidthChanged() { if (root.visible) Qt.callLater(root.centerInWindow) }
        function onHeightChanged() { if (root.visible) Qt.callLater(root.centerInWindow) }
    }

    function centerInWindow() {
        if (!rootWindow) return
        root.x = Math.round((rootWindow.width - root.width) / 2)
        root.y = Math.round((rootWindow.height - root.height) / 2)
    }
    function setLanguage(id) {
        if (root.languageIds.indexOf(id) < 0) return
        L10n.language = id
        customizationStore.language = id
    }
    function resetAppearance() {
        customizationStore.customFontPath = ""
        Theme.fontFamily = root.fontChoices[0]
        Theme.surfaceOpacity = 1.0
        Theme.reducedMotion = false
        customizationStore.reducedMotion = false
        customizationStore.customAccentStart = "#5D8DDE"
        customizationStore.customAccentEnd = "#A679D6"
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
    function selectedAccentIndex() {
        const index = root.accentIds.indexOf(customizationStore.accentColor)
        if (index >= 0) return index
        return customizationStore.accentColor === "custom" ? root.customAccentIndex : -1
    }
    function selectedGradientIndex() {
        const index = root.gradientIds.indexOf(customizationStore.accentColor)
        if (index >= 0) return index + 1
        return customizationStore.accentColor === "customGradient" ? root.customGradientIndex + 1 : 0
    }
    function applyAccent(id) {
        const isHex = /^#[0-9a-fA-F]{6}$/.test(String(id))
        if (isHex) customizationStore.customAccentStart = String(id).toUpperCase()
        const key = root.accentIds.indexOf(id) >= 0 || root.gradientIds.indexOf(id) >= 0 || id === "custom" || id === "customGradient"
            ? id : isHex ? "custom" : "orange"
        let start = root.accentValue(key === "custom" ? customizationStore.customAccentStart : key)
        let end = start
        let gradient = false
        if (key === "sunset") { start = "#EF8B72"; end = "#B36ACB"; gradient = true }
        else if (key === "ocean") { start = "#5895E8"; end = "#54C7B0"; gradient = true }
        else if (key === "aurora") { start = "#55B7A3"; end = "#9B82E8"; gradient = true }
        else if (key === "customGradient") {
            start = customizationStore.customAccentStart
            end = customizationStore.customAccentEnd
            gradient = true
        }
        Theme.accent = start
        Theme.accentSecondary = end
        Theme.accentGradientEnabled = gradient
        customizationStore.accentColor = key
        appearanceAccentChoice.currentIndex = root.selectedAccentIndex()
        appearanceGradientChoice.currentIndex = root.selectedGradientIndex()
    }
    function selectAccentTarget(target) {
        root.accentPickerTarget = target
        root.refreshAccentPalette()
    }
    function paletteTargetColor() {
        return root.accentPickerTarget === "end" ? Theme.accentSecondary : Theme.accent
    }
    function applyCustomAccent(value) {
        if (!/^#[0-9a-fA-F]{6}$/.test(String(value))) return false
        const normalized = String(value).toUpperCase()
        if (root.accentPickerTarget === "end") {
            customizationStore.customAccentEnd = normalized
            customizationStore.accentColor = "customGradient"
        } else {
            customizationStore.customAccentStart = normalized
            if (customizationStore.accentColor !== "customGradient") customizationStore.accentColor = "custom"
        }
        root.applyAccent(customizationStore.accentColor)
        return true
    }
    function clearAccentGradient() {
        if (Theme.accentGradientEnabled) root.applyAccent(String(Theme.accent))
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
            width: 276
            spacing: 10
            Row {
                width: parent.width
                spacing: 6
                PfButton {
                    objectName: "accentGradientStartButton"
                    width: (parent.width - 6) / 2
                    compact: true
                    selected: root.accentPickerTarget === "start"
                    text: L10n.t("settings.accentStopStart") + " · " + String(Theme.accent).toUpperCase()
                    onClicked: root.selectAccentTarget("start")
                }
                PfButton {
                    objectName: "accentGradientEndButton"
                    width: (parent.width - 6) / 2
                    compact: true
                    selected: root.accentPickerTarget === "end"
                    text: L10n.t("settings.accentStopEnd") + " · " + String(Theme.accentSecondary).toUpperCase()
                    onClicked: root.selectAccentTarget("end")
                }
            }
            Rectangle {
                width: parent.width
                height: 7
                radius: height / 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.accent }
                    GradientStop { position: 1; color: Theme.accentSecondary }
                }
            }
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
                    onClicked: { root.applyAccent(root.accentIds[index]); root.refreshAccentPalette() }
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
                Accessible.name: L10n.t("ui.358")
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
        const selected = root.paletteTargetColor()
        hexField.text = String(selected).toUpperCase()
        hueSlider.value = selected.hsvHue < 0 ? 0 : selected.hsvHue
        colorPlane.saturation = selected.hsvSaturation; colorPlane.brightness = selected.hsvValue
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
        if (installation.pendingValidation) return L10n.t("settings.providerRestart")
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
        if (AppInfo.providerDownloadState === "present") return root.providerStatusText(Analysis.providerChoice)
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
                    x: 4 + tabs.currentIndex * (tabs.width - 8) / 4
                    width: (tabs.width - 8) / 4; height: tabs.height - 8
                    radius: Theme.radiusButton
                    color: Theme.surfaceMuted
                    border.color: Theme.hairlineStrong
                    Behavior on x { NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.OutQuint } }
                }
            }
            TabButton {
                id: analysisTab
                objectName: "settingsAnalysisTab"
                width: (tabs.width - 8) / 4
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
                width: (tabs.width - 8) / 4
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
                id: languageTab
                objectName: "settingsLanguageTab"
                width: (tabs.width - 8) / 4
                height: tabs.height - 8
                padding: 0
                text: L10n.t("settings.tabLanguage")
                contentItem: Text { text: languageTab.text; color: languageTab.checked ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Item {}
            }
            TabButton {
                id: hotkeysTab
                objectName: "settingsHotkeysTab"
                width: (tabs.width - 8) / 4; height: tabs.height - 8
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
                        Text { text: L10n.t("updates.title"); color: Theme.secondaryText; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfComboBox { objectName: "updateChannel"; Layout.fillWidth: true; model: ["Stable", "Beta"]; currentIndex: Updates.channel === "beta" ? 1 : 0; enabled: !Updates.busy; Accessible.name: L10n.t("updates.channel"); onActivated: Updates.channel = currentIndex === 1 ? "beta" : "stable" }
                        PfCheckBox { objectName: "automaticUpdateCheck"; Layout.fillWidth: true; text: L10n.t("updates.autoCheck"); checked: Updates.automaticCheck; onToggled: Updates.automaticCheck = checked }
                        PfCheckBox { objectName: "automaticUpdateDownload"; Layout.fillWidth: true; text: L10n.t("updates.autoDownload"); checked: Updates.automaticDownload; onToggled: Updates.automaticDownload = checked }
                        PfButton { Layout.fillWidth: true; text: L10n.t("updates.check"); enabled: !Updates.busy; quiet: true; onClicked: { root.close(); Updates.check() } }
                        Text { Layout.fillWidth: true; text: L10n.t("updates.consent"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.environment"); color: Theme.secondaryText; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
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
                                objectName: "providerDownloadButton"
                                Layout.preferredWidth: 154
                                Layout.minimumWidth: 0
                                compact: true
                                text: root.selectedProviderInstalled ? L10n.t("settings.providerInstalled")
                                    : AppInfo.providerDownloading ? L10n.t("settings.providerDownloading") : L10n.t("settings.providerDownload")
                                enabled: !root.selectedProviderInstalled && !Analysis.busy && !Analysis.exportBusy
                                    && !AppInfo.backendInitializing && !AppInfo.providerDownloading && !AppInfo.providersScanning
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
                        Text { Layout.fillWidth: true; text: L10n.t("settings.cache"); color: Theme.secondaryText; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        RowLayout { Layout.fillWidth: true; spacing: 8
                            PfTextField { Layout.fillWidth: true; font.family: Theme.monoFont; text: Analysis.cachePath; placeholderText: L10n.t("settings.cachePlaceholder"); Accessible.name: L10n.t("settings.cachePath"); onEditingFinished: Analysis.setCachePath(text) }
                            PfButton { Layout.preferredWidth: 96; compact: true; text: L10n.t("settings.choose"); quiet: true; onClicked: cacheDialog.open() }
                        }
                        PfSliderField { Layout.fillWidth: true; label: L10n.t("settings.cacheLimit"); from: 0.25; to: 128; stepSize: 0.25; value: Analysis.cacheLimitGb; decimals: 2; suffix: "GB"; tooltipText: L10n.t("settings.cacheLimitHint"); Accessible.name: L10n.t("settings.cacheLimit"); onValueEdited: Analysis.setCacheLimitGb(nextValue) }
                        PfSliderField { Layout.fillWidth: true; label: L10n.t("settings.processingThreads"); from: 0; to: 64; stepSize: 1; value: Analysis.processingThreads; decimals: 0; integer: true; tooltipText: L10n.t("settings.processingThreadsHint"); Accessible.name: L10n.t("settings.processingThreads"); onValueEdited: Analysis.setProcessingThreads(Math.round(nextValue)) }
                        Text { Layout.fillWidth: true; text: L10n.t("settings.systemHint"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        PfCheckBox { objectName: "multiSelectCheck"; Layout.fillWidth: true; text: Review.t("multi"); checked: Review.multiSelect; onToggled: Review.setMulti(checked) }
                        Text { Layout.fillWidth: true; text: Review.t("multiHint"); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
                        Text { Layout.fillWidth: true; text: "MCP"; color: Theme.secondaryText; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        PfCheckBox { objectName: "mcpEnabledCheck"; text: L10n.t("ui.359"); checked: Mcp.enabled; onToggled: Mcp.enabled = checked }
                        Text { Layout.fillWidth: true; text: L10n.t("ui.360"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        PfCheckBox { objectName: "mcpControlCheck"; text: L10n.t("ui.361"); checked: Mcp.allowControl; onToggled: Mcp.allowControl = checked }
                        Text { Layout.fillWidth: true; text: L10n.t("ui.362"); color: Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
                        RowLayout { Layout.fillWidth: true; spacing: 8
                            Text { text: L10n.t("ui.363"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
                            PfTextField { objectName: "mcpPortField"; Layout.preferredWidth: 100; text: String(Mcp.port)
                                validator: IntValidator { bottom: 1024; top: 65535 }
                                onEditingFinished: if (acceptableInput) Mcp.port = Number(text)
                                onActiveFocusChanged: if (!activeFocus && !acceptableInput) text = String(Mcp.port)
                            }
                            Text { Layout.fillWidth: true; text: Mcp.error.length ? Mcp.error : Mcp.running ? (L10n.t("ui.364")) : (L10n.t("ui.365")); color: Mcp.error.length ? Theme.accent : Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        }
                        Text { Layout.fillWidth: true; visible: Mcp.running; text: Mcp.endpoint; color: Theme.textSecondary; font.family: Theme.monoFont; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }
                        RowLayout { Layout.fillWidth: true; spacing: 8
                            PfButton { objectName: "mcpCopyConnection"; Layout.fillWidth: true; compact: true; text: L10n.t("ui.366"); enabled: Mcp.running; onClicked: Mcp.copyConnection() }
                            PfButton { Layout.preferredWidth: 150; compact: true; quiet: true; text: L10n.t("ui.367"); enabled: Mcp.running; onClicked: Mcp.rotateKey() }
                        }
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
                        Text { text: L10n.t("settings.font"); color: Theme.secondaryText; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        RowLayout { width: parent.width; spacing: 8
                            PfComboBox { objectName: "appearanceFontChoice"; Layout.fillWidth: true; model: root.fontChoices; currentIndex: model.indexOf(Theme.fontFamily); Accessible.name: L10n.t("settings.font"); onActivated: root.applyFont(currentText) }
                            PfButton { Layout.preferredWidth: 132; text: L10n.t("settings.fontAdd"); quiet: true; onClicked: fontDialog.open() }
                        }
                        Text { visible: customFont.status === FontLoader.Ready; text: L10n.t("settings.fontLoaded") + ": " + customFont.name; color: Theme.secondaryText; font.family: Theme.fontFamily; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
                        Text { text: L10n.t("settings.accentColor"); color: Theme.secondaryText; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Text { text: L10n.t("settings.solidColors"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
                        PfComboBox {
                            id: appearanceAccentChoice
                            objectName: "appearanceAccentChoice"
                            width: parent.width
                            model: root.accentLabels.concat([L10n.t("settings.accentCustom")])
                            currentIndex: root.selectedAccentIndex()
                            displayText: currentIndex < 0 ? String(Theme.accent).toUpperCase() : currentText
                            Accessible.name: L10n.t("settings.solidColors")
                            onActivated: {
                                if (currentIndex < root.accentIds.length) root.applyAccent(root.accentIds[currentIndex])
                                else {
                                    root.applyAccent("custom")
                                    root.selectAccentTarget("start")
                                    root.openAccentPalette(this)
                                }
                            }
                        }
                        Text { text: L10n.t("settings.gradients"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11 }
                        PfComboBox {
                            id: appearanceGradientChoice
                            objectName: "appearanceGradientChoice"
                            width: parent.width
            model: [L10n.t("settings.gradientNone"), L10n.t("settings.accentSunset"), L10n.t("settings.accentOcean"), L10n.t("settings.accentAurora"), L10n.t("settings.accentCustomGradient")]
                            currentIndex: root.selectedGradientIndex()
                            Accessible.name: L10n.t("settings.gradients")
                            onActivated: {
                                if (currentIndex === 0) root.clearAccentGradient()
                                else if (currentIndex <= root.gradientIds.length) root.applyAccent(root.gradientIds[currentIndex - 1])
                                else {
                                    root.applyAccent("customGradient")
                                    root.selectAccentTarget("start")
                                    root.openAccentPalette(this)
                                }
                            }
                        }
                        Rectangle {
                            objectName: "appearanceAccentPreview"
                            width: parent.width
                            height: 8
                            radius: 4
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.accent }
                                GradientStop { position: 1; color: Theme.accentGradientEnabled ? Theme.accentSecondary : Theme.accent }
                            }
                        }
                        PfSliderField { width: parent.width; label: L10n.t("settings.surfaceOpacity"); from: 0.25; to: 1.0; stepSize: 0.01; value: Theme.surfaceOpacity; displayScale: 100; decimals: 0; suffix: "%"; tooltipText: L10n.t("settings.surfaceOpacityHint"); Accessible.name: L10n.t("settings.surfaceOpacity"); onValueEdited: { Theme.surfaceOpacity = nextValue; customizationStore.surfaceOpacity = nextValue } }
                        Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                        PfCheckBox { text: L10n.t("settings.reducedMotion"); checked: Theme.reducedMotion; onToggled: { Theme.reducedMotion = checked; customizationStore.reducedMotion = checked } }
                        Text { width: parent.width; text: root.themeStatus || L10n.t("settings.profileSaved"); color: root.themeStatus ? Theme.accent : Theme.textDisabled; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                }
            }
            PfReveal {
                objectName: "settingsLanguagePage"
                active: root.visible && tabs.currentIndex === 2
                distance: 18
                Flickable {
                    anchors.fill: parent; anchors.margins: 22; clip: true
                    contentWidth: width; contentHeight: languageBody.implicitHeight + 24
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: languageBody; width: parent.width; spacing: 18
                        Text { text: L10n.t("settings.language"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                        Text { width: parent.width; text: L10n.t("settings.languageHint"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        GridLayout {
                            width: parent.width; columns: 2; rowSpacing: 8; columnSpacing: 10
                            Repeater {
                                model: 6
                                delegate: Button {
                                    id: languageButton
                                    required property int index
                                    objectName: "languageButton_" + index
                                    readonly property bool currentLanguage: L10n.language === root.languageIds[index]
                                    readonly property bool flagActive: hovered || visualFocus
                                    Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 58
                                    hoverEnabled: true; focusPolicy: Qt.StrongFocus
                                    scale: hovered && !currentLanguage && !Theme.reducedMotion ? 1.008 : 1
                                    Behavior on scale { enabled: !Theme.reducedMotion; NumberAnimation { duration: 340; easing.type: Easing.InOutSine } }
                                    background: Rectangle {
                                        radius: Theme.radiusCard
                                        clip: true
                                        color: languageButton.flagActive ? "#20242d"
                                            : languageButton.down ? Theme.accentMuted
                                            : languageButton.currentLanguage ? Theme.accentMuted
                                            : languageButton.hovered ? Theme.surfaceRaised : Theme.well
                                        Behavior on color { ColorAnimation { duration: Theme.motionChangeDuration } }
                                        FlagBackdrop {
                                            objectName: "languageFlag_" + languageButton.index
                                            anchors.fill: parent
                                            country: root.languageIds[languageButton.index]
                                            active: languageButton.flagActive
                                        }
                                        Rectangle {
                                            anchors.fill: parent
                                            z: 1
                                            radius: Theme.radiusCard
                                            color: "transparent"
                                            border.width: languageButton.currentLanguage ? 1.5 : 1
                                            border.color: languageButton.currentLanguage ? Theme.accent : Theme.hairlineStrong
                                            Behavior on border.color { ColorAnimation { duration: Theme.motionChangeDuration } }
                                        }
                                    }
                                    contentItem: RowLayout {
                                        spacing: 10
                                        Item {
                                            objectName: "languageLabelSlot_" + languageButton.index
                                            Layout.fillWidth: true; Layout.fillHeight: true
                                            Text {
                                                id: languageLabel
                                                objectName: "languageLabel_" + languageButton.index
                                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                                text: root.languageNames[languageButton.index]
                                                color: languageButton.flagActive ? "#ffffff"
                                                    : languageButton.currentLanguage ? Theme.textPrimary : Theme.textSecondary
                                                style: languageButton.flagActive ? Text.Outline : Text.Normal
                                                styleColor: "#df111820"
                                                font.family: Theme.fontFamily; font.pixelSize: 14; font.weight: languageButton.currentLanguage ? Font.DemiBold : Font.Medium
                                            }
                                        }
                                        Text {
                                            visible: languageButton.currentLanguage
                                            text: "✓"; color: languageButton.flagActive ? "#ffffff" : Theme.accentText
                                            style: languageButton.flagActive ? Text.Outline : Text.Normal
                                            styleColor: "#171920"
                                            font.pixelSize: 15; font.weight: Font.Bold
                                            scale: languageButton.currentLanguage ? 1 : 0.72
                                            opacity: languageButton.currentLanguage ? 1 : 0
                                            Behavior on scale { enabled: !Theme.reducedMotion; NumberAnimation { duration: 190; easing.type: Easing.OutBack } }
                                            Behavior on opacity { enabled: !Theme.reducedMotion; NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
                                        }
                                    }
                                    onClicked: root.setLanguage(root.languageIds[index])
                                }
                            }
                        }
                    }
                }
            }
            PfReveal {
                active: root.visible && tabs.currentIndex === 3
                distance: 18
                HotkeysPage { anchors.fill: parent; anchors.margins: 22 }
            }
        }
        }
    }
}
