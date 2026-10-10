import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi

ColumnLayout {
    id: root
    property real startTime: 0
    property real endTime: 1
    property real duration: 1
    property real fps: 24
    property string label: ""
    property real viewStart: 0
    property real viewEnd: 1
    signal rangeEdited(real start, real end)
    function moveBoundary(firstMoved) {
        const start = firstMoved ? Math.min(track.first.value, track.second.value - 1 / fps) : track.first.value
        const end = firstMoved ? track.second.value : Math.max(track.second.value, track.first.value + 1 / fps)
        rangeEdited(Math.max(0, start), Math.min(duration, end))
        track.first.value = Qt.binding(function() { return root.startTime })
        track.second.value = Qt.binding(function() { return root.endTime })
    }
    function clock(seconds) {
        const value = Math.max(0, Math.round(seconds * 1000))
        return String(Math.floor(value / 60000)).padStart(2, "0") + ":" + String(Math.floor(value / 1000) % 60).padStart(2, "0") + "." + String(value % 1000).padStart(3, "0")
    }
    function fit() { viewStart = Math.max(0, startTime - 2); viewEnd = Math.min(duration, Math.max(endTime + 2, viewStart + 0.5)) }
    function zoom(factor) {
        const center = (startTime + endTime) / 2, span = Math.max(endTime - startTime + 2 / fps, (viewEnd - viewStart) * factor)
        viewStart = Math.max(0, center - span / 2); viewEnd = Math.min(duration, center + span / 2)
    }
    onStartTimeChanged: if (startTime < viewStart) fit()
    onEndTimeChanged: if (endTime > viewEnd) fit()
    onDurationChanged: fit()
    Component.onCompleted: fit()
    spacing: 3
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: root.label; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 12 }
        PfIconButton { width: 26; height: 26; iconSource: "qrc:/qt/qml/PfUi/qml/assets/minus.svg"; accessibleName: L10n.language === "ru" ? "Уменьшить масштаб" : "Zoom out"; onClicked: root.zoom(2) }
        PfIconButton { width: 26; height: 26; iconSource: "qrc:/qt/qml/PfUi/qml/assets/plus.svg"; accessibleName: L10n.language === "ru" ? "Увеличить масштаб" : "Zoom in"; onClicked: root.zoom(0.5) }
    }
    Canvas {
        id: ruler; Layout.fillWidth: true; height: 22
        Connections { target: root; function onViewStartChanged() { ruler.requestPaint() } function onViewEndChanged() { ruler.requestPaint() } }
        onWidthChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.clearRect(0, 0, width, height)
            c.strokeStyle = Theme.hairlineStrong; c.fillStyle = Theme.textSecondary; c.font = "9px \"" + Theme.fontFamily + "\""
            for (let i = 0; i <= 4; ++i) {
                const x = 6 + i * (width - 12) / 4
                c.beginPath(); c.moveTo(x, 16); c.lineTo(x, 22); c.stroke()
                c.textAlign = i === 0 ? "left" : i === 4 ? "right" : "center"
                c.fillText(root.clock(root.viewStart + (root.viewEnd - root.viewStart) * i / 4), x, 12)
            }
        }
    }
    RangeSlider {
        id: track
        objectName: "pairRangeTimeline"
        Layout.fillWidth: true; height: 36
        from: root.viewStart; to: Math.max(from + 1 / root.fps, root.viewEnd)
        stepSize: 1 / root.fps
        first.value: root.startTime; second.value: root.endTime
        first.onMoved: root.moveBoundary(true)
        second.onMoved: root.moveBoundary(false)
        background: Rectangle {
            x: track.leftPadding; y: track.topPadding + (track.availableHeight - height) / 2
            width: track.availableWidth; height: 22; radius: 5; color: Theme.well; border.color: Theme.hairlineStrong
            Rectangle { x: track.first.position * parent.width; width: Math.max(0, (track.second.position - track.first.position) * parent.width); height: parent.height; radius: 4; color: Theme.accentMuted; border.color: Theme.accent }
        }
        first.handle: Rectangle {
            x: track.leftPadding + track.first.visualPosition * (track.availableWidth - width); y: track.topPadding + (track.availableHeight - height) / 2
            width: 20; height: 34; radius: 4; color: track.first.pressed ? Theme.textPrimary : Theme.accent
            Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
        }
        second.handle: Rectangle {
            x: track.leftPadding + track.second.visualPosition * (track.availableWidth - width); y: track.topPadding + (track.availableHeight - height) / 2
            width: 20; height: 34; radius: 4; color: track.second.pressed ? Theme.textPrimary : Theme.accent
            Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
        }
    }
    Text { Layout.fillWidth: true; text: root.clock(root.startTime) + "  —  " + root.clock(root.endTime); color: Theme.textSecondary; font.family: Theme.monoFont; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter }
}
