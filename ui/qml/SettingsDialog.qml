import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import PfUi
import PfUiBridge

Popup {
    id: root
    property Item rootWindow
    modal: true; focus: true; padding: 0
    width: Math.min(720, rootWindow ? rootWindow.width - 40 : 680); height: Math.min(700, rootWindow ? rootWindow.height - 40 : 640)
    x: rootWindow ? Math.round((rootWindow.width - width) / 2) : 0; y: rootWindow ? Math.round((rootWindow.height - height) / 2) : 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.70) }
    enter: Transition { NumberAnimation { properties: "opacity,scale"; from: 0.92; to: 1; duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { properties: "opacity,scale"; to: 0.92; duration: Theme.motionDuration } }
    background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairline; layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.78); shadowBlur: 1.0; shadowVerticalOffset: 18 } }
    FileDialog { id: modelDialog; title: L10n.t("settings.modelPath"); fileMode: FileDialog.OpenFile; nameFilters: ["ONNX (*.onnx)", L10n.t("dialog.allFiles")]; onAccepted: Analysis.setModelPath(selectedFile.toString().replace(/^file:\/\//, "")) }
    FolderDialog { id: cacheDialog; title: L10n.t("settings.cachePath"); onAccepted: Analysis.setCachePath(selectedFolder.toString().replace(/^file:\/\//, "")) }

    contentItem: Column { spacing: 0
        Rectangle { width: parent.width; height: 58; color: Theme.surfaceRaised; radius: Theme.radiusOverlay
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
            Text { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; text: L10n.t("settings.windowTitle"); color: Theme.textPrimary; font.pixelSize: 13; font.weight: Font.DemiBold }
            PfIconButton { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; iconSource: "qrc:/qt/qml/PfUi/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: root.close() }
            MouseArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.rightMargin: 58; property real pressX; property real pressY
                onPressed: { pressX = mouse.x; pressY = mouse.y }
                onPositionChanged: if (pressed && rootWindow) { root.x = Math.max(12, Math.min(rootWindow.width - root.width - 12, root.x + mouse.x - pressX)); root.y = Math.max(12, Math.min(rootWindow.height - root.height - 12, root.y + mouse.y - pressY)) }
            }
        }
        Flickable { width: parent.width; height: parent.height - 58; clip: true; contentWidth: width; contentHeight: body.implicitHeight + 48; boundsBehavior: Flickable.StopAtBounds; ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column { id: body; width: parent.width - 48; x: 24; y: 24; spacing: 12
                Text { text: L10n.t("settings.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 27 }
                Text { text: L10n.t("settings.subtitle"); color: Theme.textSecondary; font.pixelSize: 12 }
                Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                Text { text: L10n.t("settings.environment"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                Row { width: parent.width; height: 36; spacing: 12
                    Text { width: 135; text: L10n.t("settings.provider"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                    ComboBox { id: provider; width: 180; height: 32; model: ["Auto", "TensorRT", "CUDA", "DirectML", "CPU"]; currentIndex: ["auto", "tensorrt", "cuda", "dml", "cpu"].indexOf(Analysis.providerChoice); Accessible.name: L10n.t("settings.provider"); onActivated: Analysis.providerChoice = ["auto", "tensorrt", "cuda", "dml", "cpu"][currentIndex] }
                    Text { text: AppInfo.gpuSummary; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; width: parent.width - 340 }
                }
                Row { width: parent.width; height: 34; spacing: 12
                    Text { width: 135; text: L10n.t("settings.modelPath"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                    TextField { width: parent.width - 220; text: Analysis.modelPath; placeholderText: "models / yolo26m-pose.onnx"; Accessible.name: L10n.t("settings.modelPath"); onEditingFinished: Analysis.setModelPath(text) }
                    PfButton { width: 70; text: L10n.t("settings.browse"); quiet: true; onClicked: modelDialog.open() }
                }
                Row { width: parent.width; height: 34; spacing: 12
                    Text { width: 135; text: L10n.t("settings.cachePath"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                    TextField { width: parent.width - 220; text: Analysis.cachePath; placeholderText: "%LocalAppData%/ParallelFinder/cache"; Accessible.name: L10n.t("settings.cachePath"); onEditingFinished: Analysis.setCachePath(text) }
                    PfButton { width: 70; text: L10n.t("settings.browse"); quiet: true; onClicked: cacheDialog.open() }
                }
                Row { width: parent.width; height: 34; spacing: 12
                    Text { width: 135; text: L10n.t("settings.cacheLimit"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                    PfSlider { width: parent.width - 230; from: 0.25; to: 128; stepSize: 0.25; value: Analysis.cacheLimitGb; onValueChanged: if (Math.abs(Analysis.cacheLimitGb - value) > 0.001) Analysis.cacheLimitGb = value }
                    Text { width: 70; text: Analysis.cacheLimitGb.toFixed(2); color: Theme.accent; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                Text { text: L10n.t("settings.scene"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                Row { width: parent.width; height: 34; spacing: 12
                    Text { width: 135; text: L10n.t("settings.sceneThreshold"); color: Theme.textPrimary; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                    PfSlider { width: parent.width - 230; from: 1; to: 255; stepSize: 1; value: Analysis.sceneThreshold; onValueChanged: if (Math.abs(Analysis.sceneThreshold - value) > 0.1) Analysis.sceneThreshold = value }
                    Text { width: 70; text: Math.round(Analysis.sceneThreshold); color: Theme.accent; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                Text { text: L10n.t("settings.matcher"); color: Theme.sage; font.pixelSize: 11; font.weight: Font.DemiBold }
                Text { text: L10n.t("settings.matcherHint"); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                PfCheckBox { text: L10n.t("settings.reducedMotion"); checked: Theme.reducedMotion; onToggled: Theme.reducedMotion = checked }
                PfButton { width: parent.width; text: L10n.t("settings.reset"); quiet: true; onClicked: root.resetRequested() }
                Text { text: L10n.t("settings.saved"); color: Theme.textDisabled; font.pixelSize: 10 }
            }
        }
    }
    signal resetRequested()
}
