import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi

ComboBox {
    id: control

    implicitHeight: 34
    height: 34
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: 12

    contentItem: Text {
        leftPadding: 12
        rightPadding: 30
        text: control.displayText
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Image {
        x: control.width - width - 10
        y: Math.round((control.height - height) / 2)
        width: 12
        height: 12
        source: "qrc:/qt/qml/PfUi/assets/chevron-right.svg"
        rotation: 90
        opacity: control.enabled ? (control.hovered || control.activeFocus ? 0.95 : 0.65) : 0.3
    }

    background: Rectangle {
        radius: Theme.radiusButton
        color: control.activeFocus ? Theme.surfaceRaised : (control.hovered ? Theme.surfaceRaised : Theme.well)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.hairline
    }

    delegate: ItemDelegate {
        required property int index
        width: control.width - 8
        height: 32
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        contentItem: Text {
            leftPadding: 10
            text: control.model[index]
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusButton
            color: parent.highlighted ? Theme.accentMuted : (parent.hovered ? Theme.surfaceRaised : "transparent")
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, 220)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }
        background: Rectangle {
            radius: Theme.radiusButton
            color: Theme.surfaceRaised
            border.color: Theme.hairlineStrong
            layer.enabled: true
        }
    }
}
