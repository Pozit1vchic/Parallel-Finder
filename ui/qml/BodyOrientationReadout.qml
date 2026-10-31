import QtQuick
import QtQuick.Layouts
import PfUi
import "OrientationEvidence.js" as Orientation

ColumnLayout {
    id: root
    objectName: "bodyOrientationReadout"
    property var leftDescriptor: ({})
    property var rightDescriptor: ({})
    spacing: 8

    function supported(d) {
        return Orientation.bodySupported(d)
    }
    function direction(d) {
        if (supported(d)) return L10n.t("orientation.view" + d.bodyView)
        return L10n.t(d && d.bodyYawEvidence === 1 ? "orientation.ambiguous" : "orientation.unknown")
    }
    function angle(d) {
        if (!supported(d)) return ""
        // Keep the measured continuous angle; the direction name is a label.
        const rounded = Math.round(d.bodyYaw * 10) / 10
        return (rounded > 0 ? "+" : "") + rounded.toLocaleString(Qt.locale(L10n.language), "f", 1) + "°"
    }
    function support(d) {
        if (!d || !Number.isInteger(d.bodyYawRequestedSamples) || d.bodyYawRequestedSamples <= 0
            || !Number.isInteger(d.bodyYawObservedSamples) || d.bodyYawObservedSamples < 0
            || d.bodyYawObservedSamples > d.bodyYawRequestedSamples) return ""
        return L10n.t("orientation.support").replace("%1", d.bodyYawObservedSamples).replace("%2", d.bodyYawRequestedSamples)
    }

    Text {
        objectName: "bodyOrientationTitle"
        Layout.fillWidth: true
        text: L10n.t("orientation.title")
        color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 16
        Repeater {
            model: ["left", "right"]
            delegate: ColumnLayout {
                required property string modelData
                readonly property var descriptor: (modelData === "left" ? root.leftDescriptor : root.rightDescriptor) || {}
                Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.alignment: Qt.AlignTop
                spacing: 4
                Text {
                    Layout.fillWidth: true; text: modelData === "left" ? "A" : "B"
                    color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11
                }
                Text {
                    objectName: "bodyDirection" + (modelData === "left" ? "Left" : "Right")
                    Layout.fillWidth: true; text: root.direction(descriptor)
                    color: root.supported(descriptor) ? Theme.textPrimary : Theme.textSecondary
                    font.family: Theme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap
                }
                Text {
                    objectName: "bodyAngle" + (modelData === "left" ? "Left" : "Right")
                    Layout.fillWidth: true; text: root.angle(descriptor); visible: text.length > 0
                    color: Theme.textPrimary; font.family: Theme.monoFont; font.pixelSize: 12
                }
                Text {
                    objectName: "bodySupport" + (modelData === "left" ? "Left" : "Right")
                    Layout.fillWidth: true; text: root.support(descriptor); visible: text.length > 0
                    color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap
                }
            }
        }
    }
    Text {
        Layout.fillWidth: true; text: L10n.t("orientation.frameReference")
        color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10; wrapMode: Text.WordWrap
    }
}
