import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi

// A compact After-Effects-style numeric control: the rail remains quick for
// scrubbing, while the value field is precise and keyboard-friendly.
Item {
    id: root
    property string label: ""
    property string suffix: ""
    property string tooltipText: ""
    property real from: 0
    property real to: 1
    property real stepSize: 0.01
    property real value: from
    property real displayScale: 1
    property int decimals: 2
    property bool integer: false
    signal valueEdited(real nextValue)

    implicitHeight: labelText.implicitHeight + 4 + 24 + 13
    height: implicitHeight

    function displayNumber(raw) {
        const scaled = Number(raw) * root.displayScale
        if (root.integer) return String(Math.round(scaled))
        return scaled.toFixed(root.decimals)
    }

    function parseInput(rawText) {
        const normalized = String(rawText || "").replace(",", ".").replace(/[^0-9+\-.]/g, "")
        const parsed = Number(normalized)
        if (!Number.isFinite(parsed)) return Number(root.value)
        return Math.max(root.from, Math.min(root.to, parsed / root.displayScale))
    }

    function commitInput() {
        const next = root.parseInput(valueField.text)
        valueField.text = root.displayNumber(next)
        root.valueEdited(next)
    }

    // The explanation belongs to the whole control, not only the tiny rail.
    // This keeps the interaction discoverable when the pointer is over the
    // label or the editable number as well.
    HoverHandler { id: hintHover }
    ToolTip.visible: hintHover.hovered && root.tooltipText.length > 0
    ToolTip.text: root.tooltipText
    ToolTip.delay: 350
    ToolTip.timeout: 6000

    onValueChanged: if (!valueField.activeFocus) valueField.text = root.displayNumber(root.value)

    Row {
        id: labelRow
        width: parent.width
        height: labelText.implicitHeight
        Text {
            id: labelText
            width: parent.width
            text: root.label
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }

    RowLayout {
        id: controlRow
        anchors.top: labelRow.bottom
        anchors.topMargin: 4
        width: parent.width
        spacing: 8
        PfSlider {
            id: slider
            Layout.fillWidth: true
            Layout.minimumWidth: 48
            from: root.from
            to: root.to
            stepSize: root.stepSize
            value: root.value
            tooltipText: root.tooltipText
            Accessible.name: root.label
            onMoved: root.valueEdited(value)
        }
        PfTextField {
            id: valueField
            Layout.preferredWidth: 62
            Layout.minimumWidth: 54
            Layout.maximumWidth: 72
            Layout.alignment: Qt.AlignVCenter
            height: 24
            implicitHeight: 24
            text: root.displayNumber(root.value)
            horizontalAlignment: Text.AlignRight
            font.pixelSize: 10
            validator: DoubleValidator {
                bottom: root.from * root.displayScale
                top: root.to * root.displayScale
                decimals: root.integer ? 0 : root.decimals
                notation: DoubleValidator.StandardNotation
            }
            Accessible.name: root.label + " value"
            onAccepted: root.commitInput()
            onEditingFinished: root.commitInput()
        }
        Text {
            id: suffixText
            Layout.minimumWidth: root.suffix.length ? implicitWidth : 0
            Layout.maximumWidth: root.suffix.length ? implicitWidth : 0
            text: root.suffix
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: 10
            Layout.alignment: Qt.AlignVCenter
        }
    }

    RowLayout {
        id: rangeRow
        anchors.top: controlRow.bottom
        anchors.topMargin: 1
        width: parent.width
        height: 12
        Text {
            Layout.minimumWidth: 0
            text: root.displayNumber(root.from) + (root.suffix.length ? root.suffix : "")
            color: Theme.textDisabled
            font.family: Theme.fontFamily
            font.pixelSize: 9
            elide: Text.ElideRight
        }
        Item { Layout.fillWidth: true }
        Text {
            Layout.minimumWidth: 0
            text: root.displayNumber(root.to) + (root.suffix.length ? root.suffix : "")
            color: Theme.textDisabled
            font.family: Theme.fontFamily
            font.pixelSize: 9
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
        }
    }
}
