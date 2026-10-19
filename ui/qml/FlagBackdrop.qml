import QtQuick
import PfUi

// Staggered clipped strips paint the actual vector flag across the card.
// No timer or off-screen animation remains active after the interaction.
Item {
    id: root
    property string country: "ru"
    property bool active: false
    property real reveal: active ? 1 : 0
    clip: true
    Behavior on reveal { NumberAnimation { duration: Theme.reducedMotion ? 0 : root.active ? 560 : 220; easing.type: Easing.OutCubic } }
    Repeater {
        model: 9
        delegate: Item {
            required property int index
            readonly property real progress: Math.max(0, Math.min(1, (root.reveal - index * 0.025) / (1 - index * 0.025)))
            x: 0; y: index * root.height / 9
            width: root.width * progress; height: root.height / 9 + 1
            clip: true
            Image {
                width: root.width; height: root.height; y: -parent.y
                source: "qrc:/qt/qml/PfUi/qml/assets/flags/" + root.country + ".svg"
                sourceSize.width: 600; sourceSize.height: 200
                fillMode: Image.Stretch
            }
        }
    }
}
