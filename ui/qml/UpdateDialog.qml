import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi
import PfUiBridge

Popup {
    id: root
    objectName: "updateDialog"
    property var service: Updates
    property var rootWindow
    readonly property bool transferring: root.service.state === "downloading" || root.service.state === "verifying"
    modal: true; focus: true; padding: 0
    width: Math.min(560, rootWindow ? rootWindow.width - 40 : 560)
    height: Math.min(520, updateColumn.implicitHeight + 48)
    x: rootWindow ? (rootWindow.width - width) / 2 : 0
    y: rootWindow ? (rootWindow.height - height) / 2 : 0
    closePolicy: root.service.state === "installing" ? Popup.NoAutoClose : Popup.CloseOnEscape
    onClosed: root.service.later()
    Connections {
        target: root.service
        function onChanged() { if (root.service.dialogVisible) root.open(); else root.close() }
    }
    Component.onCompleted: if (root.service.dialogVisible) open()
    enter: Transition { ParallelAnimation {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.reducedMotion ? 0 : 180; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.reducedMotion ? 0 : 180; easing.type: Easing.OutCubic }
    } }
    exit: Transition { ParallelAnimation {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.reducedMotion ? 0 : 150; easing.type: Easing.InCubic }
        NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: Theme.reducedMotion ? 0 : 150; easing.type: Easing.InCubic }
    } }
    Overlay.modal: Rectangle { color: GraphicsInfo.api === GraphicsInfo.Software ? "#99000000" : "transparent" }
    background: Rectangle {
        color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairline
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowOverlay; shadowOpacity: 0.78; shadowBlur: 1.0; shadowVerticalOffset: 20 }
    }
    contentItem: Flickable {
        clip: true; contentWidth: width; contentHeight: updateColumn.implicitHeight + 48
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: updateColumn; x: 24; y: 24; width: parent.width - 48; spacing: 14
            Item {
                width: parent.width; height: 44
                Text { text: L10n.t("updates.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 25 }
                PfIconButton { anchors.right: parent.right; iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"; accessibleName: L10n.t("common.close"); enabled: root.service.state !== "installing"; onClicked: root.service.later() }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.hairline }
            Text {
                width: parent.width; text: root.service.currentVersion + (root.service.newVersion.length ? "  →  " + root.service.newVersion : "")
                color: Theme.textPrimary; font.family: Theme.monoFont; font.pixelSize: 13; wrapMode: Text.Wrap
            }
            Text { visible: root.service.totalBytes > 0; text: L10n.t("updates.size") + " · " + (root.service.totalBytes / 1048576).toFixed(1) + " MB"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
            Text {
                width: parent.width; visible: root.service.changelog.length > 0 && !root.transferring
                text: root.service.changelog; textFormat: Text.PlainText; maximumLineCount: 8; elide: Text.ElideRight
                wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 13
            }
            Text {
                width: parent.width; text: L10n.t("updates.state." + root.service.state); wrapMode: Text.WordWrap
                color: root.service.state === "error" ? Theme.accent : Theme.sage; font.family: Theme.fontFamily; font.pixelSize: 13
            }
            Rectangle {
                objectName: "updateProgress"; width: parent.width; height: 6; radius: 3; color: Theme.surfaceMuted; visible: root.transferring
                Rectangle { width: parent.width * root.service.progress; height: parent.height; radius: 3; color: Theme.sage
                    Behavior on width { NumberAnimation { duration: Theme.motionDuration } }
                }
                Accessible.role: Accessible.ProgressBar; Accessible.name: Math.round(root.service.progress * 100) + "%"
            }
            Text {
                visible: root.transferring; width: parent.width
                text: Math.round(root.service.progress * 100) + "%  ·  " + (root.service.receivedBytes / 1048576).toFixed(1) + " / " + (root.service.totalBytes / 1048576).toFixed(1) + " MB  ·  " + (root.service.bytesPerSecond / 1048576).toFixed(1) + " MB/s"
                color: Theme.textSecondary; font.family: Theme.monoFont; font.pixelSize: 12; wrapMode: Text.Wrap
            }
            Text { visible: root.service.error.length > 0; width: parent.width; text: root.service.error; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 12 }
            Text { visible: root.service.state === "available" || root.service.state === "ready"; width: parent.width; text: L10n.t("updates.consent"); wrapMode: Text.WordWrap; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 12 }
            Row {
                width: parent.width; spacing: 8
                PfButton {
                    objectName: "updatePrimaryButton"; width: (parent.width - 16) / 3; primary: true
                    visible: root.service.state !== "checking" && root.service.state !== "installing"
                    enabled: !root.service.busy || root.transferring
                    text: root.transferring ? L10n.t("updates.cancel") : root.service.state === "ready" ? L10n.t("updates.install") : root.service.state === "available" ? L10n.t("updates.update") : L10n.t("updates.check")
                    onClicked: { if (root.transferring) root.service.cancel(); else if (root.service.state === "ready") root.service.install(); else if (root.service.state === "available") root.service.download(); else root.service.check() }
                }
                PfButton { width: (parent.width - 16) / 3; text: L10n.t("updates.later"); quiet: true; enabled: root.service.state !== "installing"; onClicked: root.service.later() }
                PfButton { width: (parent.width - 16) / 3; text: L10n.t("updates.skip"); quiet: true; visible: root.service.newVersion.length > 0; enabled: !root.service.busy; onClicked: root.service.skip() }
            }
        }
    }
}
