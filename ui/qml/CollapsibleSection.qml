import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi

// A layout-owned disclosure row. Its body participates in implicitHeight only
// while expanded, so a narrow rail never grows outside its Flickable.
Item {
    id: root
    property string title: ""
    property string summary: ""
    property bool expanded: false
    property real reveal: 0
    clip: true
    Component.onCompleted: reveal = expanded ? 1 : 0
    onExpandedChanged: {
        if (!expandReveal) return
        expandReveal.stop()
        if (Theme.reducedMotion) reveal = expanded ? 1 : 0
        else { expandReveal.to = expanded ? 1 : 0; expandReveal.start() }
    }
    NumberAnimation { id: expandReveal; target: root; property: "reveal"; duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic }
    Connections {
        target: Theme
        function onReducedMotionChanged() {
            if (Theme.reducedMotion) { expandReveal.stop(); root.reveal = root.expanded ? 1 : 0 }
        }
    }
    default property alias content: body.data
    signal toggled(bool expanded)

    // Use the body's actual child bounds instead of relying on a layout pass
    // that may happen after Flickable has already calculated contentHeight.
    // This keeps every advanced card scrollable when the disclosure opens.
    implicitHeight: header.implicitHeight + (body.implicitHeight + 10) * reveal
    height: implicitHeight

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 10

        Button {
            id: header
            objectName: "disclosureButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: Math.max(46, titleText.implicitHeight + 16)
            height: implicitHeight
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: root.title
            onClicked: {
                // The owner updates expanded; never break its binding here.
                root.toggled(!root.expanded)
            }
            contentItem: Text {
                id: titleText
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 28
                text: root.title
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: 11
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                clip: true
            }
            background: Rectangle {
                radius: Theme.radiusButton
                color: header.hovered ? Theme.surfaceRaised : "transparent"
                border.width: header.visualFocus ? 2 : 1
                border.color: header.visualFocus ? Theme.accent : Theme.hairline
                Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionDuration } }
            }
            Image {
                anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                width: 12; height: 12; source: "qrc:/qt/qml/PfUi/qml/assets/chevron-right.svg"
                rotation: root.expanded ? 90 : 0; opacity: 0.7
                Behavior on rotation { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.OutCubic } }
            }
        }

        Column {
            id: body
            width: parent.width
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 10
            visible: root.reveal > 0
            opacity: root.reveal
        }
    }
}
