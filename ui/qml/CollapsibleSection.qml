import QtQuick
import QtQuick.Controls.Basic
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

    implicitHeight: header.implicitHeight + (root.expanded ? body.implicitHeight + 10 : 0)
    height: implicitHeight

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 10

        Button {
            id: header
            width: parent.width
            implicitHeight: 44
            height: implicitHeight
            hoverEnabled: true
            Accessible.role: Accessible.Button
            Accessible.name: root.title
            onClicked: {
                // Let the owner keep the binding; assigning root.expanded here
                // would sever an external `expanded: ownerState` binding.
                root.toggled(!root.expanded)
            }
            contentItem: Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                Text {
                    width: parent.width - disclosure.implicitWidth - 8
                    text: root.title
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    id: disclosure
                    text: root.expanded ? "−" : "+"
                    color: Theme.accent
                    font.pixelSize: 17
                    verticalAlignment: Text.AlignVCenter
                }
            }
            background: Rectangle {
                radius: Theme.radiusButton
                color: header.hovered ? Theme.surfaceRaised : "transparent"
                border.width: header.activeFocus ? 2 : 1
                border.color: header.activeFocus ? Theme.accent : Theme.hairline
            }
        }

        Column {
            id: body
            width: parent.width
            spacing: 10
            visible: root.expanded
        }
    }
}
