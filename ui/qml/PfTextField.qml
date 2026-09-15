import QtQuick
import QtQuick.Controls.Basic
import PfUi

TextField {
    id: control

    implicitHeight: 34
    height: 34
    leftPadding: 12
    rightPadding: 12
    color: Theme.textPrimary
    placeholderTextColor: Theme.textDisabled
    font.family: Theme.fontFamily
    font.pixelSize: 12
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radiusButton
        color: control.visualFocus ? Theme.surfaceRaised : Theme.well
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.hairline
    }
}
