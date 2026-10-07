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
    property bool presentationBlocked: false
    property real positionOffsetX: 0
    property real positionOffsetY: 0
    readonly property bool transferring: root.service.state === "downloading" || root.service.state === "verifying"
    modal: true; focus: true; padding: 0
    width: Math.min(560, rootWindow ? rootWindow.width - 40 : 560)
    height: Math.min(520, updateColumn.implicitHeight + 48, rootWindow ? Math.max(1, rootWindow.height - 24) : 520)
    Behavior on height { NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic } }
    x: rootWindow ? Math.max(12, Math.min(rootWindow.width - width - 12, (rootWindow.width - width) / 2 + positionOffsetX)) : 0
    y: rootWindow ? Math.max(12, Math.min(rootWindow.height - height - 12, (rootWindow.height - height) / 2 + positionOffsetY)) : 0
    onOpened: { positionOffsetX = 0; positionOffsetY = 0 }
    closePolicy: root.service.state === "installing" ? Popup.NoAutoClose : Popup.CloseOnEscape
    onClosed: root.service.later()
    Connections {
        target: root.service
        function onChanged() { root.syncPresentation() }
    }
    function syncPresentation() {
        if (!root.service.dialogVisible) { if (root.visible) root.close() }
        else if (!root.presentationBlocked && !root.visible) root.open()
    }
    onPresentationBlockedChanged: syncPresentation()
    Component.onCompleted: syncPresentation()
    enter: Transition { ParallelAnimation {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionRevealDuration; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.985; to: 1; duration: Theme.motionRevealDuration; easing.type: Easing.OutQuint }
    } }
    exit: Transition { ParallelAnimation {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
        NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
    } }
    Overlay.modal: Rectangle { color: GraphicsInfo.api === GraphicsInfo.Software ? "#99000000" : "transparent" }
    background: Rectangle {
        color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairline
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowOverlay; shadowOpacity: 0.78; shadowBlur: 1.0; shadowVerticalOffset: 20 }
    }
    contentItem: Flickable {
        clip: true; contentWidth: width; contentHeight: updateColumn.implicitHeight + 48
        Behavior on contentHeight { NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic } }
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: updateColumn; x: 24; y: 24; width: parent.width - 48; spacing: 14
            Item {
                width: parent.width; height: 44
                Text { text: L10n.t("updates.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 25 }
                PfIconButton { anchors.right: parent.right; iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"; accessibleName: L10n.t("common.close"); enabled: root.service.state !== "installing"; onClicked: root.service.later() }
                MouseArea {
                    objectName: "updateDragArea"
                    anchors.fill: parent; anchors.rightMargin: 48
                    enabled: !!root.rootWindow
                    preventStealing: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property point pressPoint
                    property point startPosition
                    onPressed: function(mouse) {
                        pressPoint = mapToItem(root.rootWindow.contentItem, mouse.x, mouse.y)
                        startPosition = Qt.point(root.x, root.y)
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const point = mapToItem(root.rootWindow.contentItem, mouse.x, mouse.y)
                        const nextX = Math.max(12, Math.min(root.rootWindow.width - root.width - 12, startPosition.x + point.x - pressPoint.x))
                        const nextY = Math.max(12, Math.min(root.rootWindow.height - root.height - 12, startPosition.y + point.y - pressPoint.y))
                        root.positionOffsetX = nextX - (root.rootWindow.width - root.width) / 2
                        root.positionOffsetY = nextY - (root.rootWindow.height - root.height) / 2
                    }
                }
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
                id: updateStatusText
                width: parent.width; text: L10n.t("updates.state." + root.service.state); wrapMode: Text.WordWrap
                NumberAnimation { id: statusFade; target: updateStatusText; property: "opacity"; from: 0; to: 1; duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic }
                onTextChanged: statusFade.restart()
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
                    text: root.transferring ? L10n.t("updates.cancel") : root.service.state === "ready" ? L10n.t("updates.install") : root.service.state === "available" || root.service.state === "cancelled" ? L10n.t("updates.update") : L10n.t("updates.check")
                    onClicked: { if (root.transferring) root.service.cancel(); else if (root.service.state === "ready") root.service.install(); else if (root.service.state === "available" || root.service.state === "cancelled") root.service.download(); else root.service.check() }
                }
                PfButton { width: (parent.width - 16) / 3; text: L10n.t("updates.later"); quiet: true; enabled: root.service.state !== "installing"; onClicked: root.service.later() }
                PfButton { width: (parent.width - 16) / 3; text: L10n.t("updates.skip"); quiet: true; visible: root.service.newVersion.length > 0; enabled: !root.service.busy; onClicked: root.service.skip() }
            }
        }
    }
}
