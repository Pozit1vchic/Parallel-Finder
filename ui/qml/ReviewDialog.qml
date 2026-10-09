import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi

Popup {
    id: root
    property var rootWindow
    property string title: ""
    property bool backdropClosing: false
    default property alias body: bodyItem.data
    width: Math.min(820, rootWindow ? rootWindow.width - 32 : 780)
    height: Math.min(650, rootWindow ? rootWindow.height - 32 : 620)
    x: rootWindow ? Math.round((rootWindow.width - width) / 2) : 0
    y: rootWindow ? Math.round((rootWindow.height - height) / 2) : 0
    padding: 22; modal: true; focus: true
    onAboutToShow: backdropClosing = false
    onAboutToHide: backdropClosing = true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: GraphicsInfo.api === GraphicsInfo.Software ? "#99000000" : "transparent" }
    background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairlineStrong }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.motionRevealDuration; easing.type: Easing.OutQuint }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic } }
    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: root.title; color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 25; elide: Text.ElideRight }
            PfIconButton { iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"; accessibleName: Review.t("close"); onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }
        Item { id: bodyItem; Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
