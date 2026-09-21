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
    default property alias content: body.data
    signal toggled(bool expanded)

    // Use the body's actual child bounds instead of relying on a layout pass
    // that may happen after Flickable has already calculated contentHeight.
    // This keeps every advanced card scrollable when the disclosure opens.
    implicitHeight: header.implicitHeight + (root.expanded ? body.implicitHeight + 10 : 0)
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
                anchors.rightMargin: 12
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
            }
        }

        Column {
            id: body
            width: parent.width
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 10
            visible: root.expanded
        }
    }
}
