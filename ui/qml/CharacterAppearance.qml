import QtQuick
import PfUi

Item {
    id: root
    objectName: "characterAppearance"
    property alias source: frame.source
    property var personBox: null
    readonly property bool validBox: !!personBox
        && isFinite(personBox.left) && isFinite(personBox.top) && isFinite(personBox.right) && isFinite(personBox.bottom)
        && personBox.right > Math.max(0,personBox.left) && personBox.bottom > Math.max(0,personBox.top)
        && personBox.left < 1 && personBox.top < 1
    readonly property bool marked: validBox && frame.status === Image.Ready && frame.paintedWidth > 0 && frame.paintedHeight > 0
    readonly property real imageLeft: (width-frame.paintedWidth)/2
    readonly property real imageTop: (height-frame.paintedHeight)/2
    readonly property real personLeft: imageLeft + (marked ? Math.max(0,personBox.left) : 0)*frame.paintedWidth
    readonly property real personTop: imageTop + (marked ? Math.max(0,personBox.top) : 0)*frame.paintedHeight
    readonly property real personRight: imageLeft + (marked ? Math.min(1,personBox.right) : 1)*frame.paintedWidth
    readonly property real personBottom: imageTop + (marked ? Math.min(1,personBox.bottom) : 1)*frame.paintedHeight
    clip: true
    Image { id: frame; anchors.fill: parent; fillMode: Image.PreserveAspectFit; asynchronous: true }
    // Leave the target untouched; quiet shading separates overlapping people.
    Item {
        visible: root.marked
        anchors.fill: parent
        Rectangle { x: root.imageLeft; y: root.imageTop; width: frame.paintedWidth; height: Math.max(0,root.personTop-y); color: "#60000000" }
        Rectangle { x: root.imageLeft; y: root.personBottom; width: frame.paintedWidth; height: Math.max(0,root.imageTop+frame.paintedHeight-y); color: "#60000000" }
        Rectangle { x: root.imageLeft; y: root.personTop; width: Math.max(0,root.personLeft-x); height: root.personBottom-y; color: "#60000000" }
        Rectangle { x: root.personRight; y: root.personTop; width: Math.max(0,root.imageLeft+frame.paintedWidth-x); height: root.personBottom-y; color: "#60000000" }
    }
    Canvas {
        id: corners
        anchors.fill: parent
        visible: root.marked
        property color markerColor: Theme.accent
        onMarkerColorChanged: requestPaint()
        onAvailableChanged: if (available) requestPaint()
        onVisibleChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx=getContext("2d");ctx.reset()
            if(!root.marked)return
            const l=Math.max(2,root.personLeft),t=Math.max(2,root.personTop)
            const r=Math.min(width-2,root.personRight),b=Math.min(height-2,root.personBottom)
            if(r<=l || b<=t)return
            const length=Math.min(18,(r-l)*.28,(b-t)*.28),radius=Math.min(5,length*.45)
            ctx.lineCap="round";ctx.lineJoin="round"
            function corner(x,y,h,v) {
                ctx.beginPath();ctx.moveTo(x,y+v*length);ctx.lineTo(x,y+v*radius)
                ctx.quadraticCurveTo(x,y,x+h*radius,y);ctx.lineTo(x+h*length,y);ctx.stroke()
            }
            function outline(color,thickness) {
                ctx.strokeStyle=color;ctx.lineWidth=thickness
                corner(l,t,1,1);corner(r,t,-1,1);corner(l,b,1,-1);corner(r,b,-1,-1)
            }
            // Two quiet keylines preserve contrast even on matching scenery
            // or when the user chooses a very dark/light accent color.
            outline("#D9000000",5);outline("#CFFFFFFF",3.2);outline(markerColor,2)
        }
    }
    onPersonLeftChanged: corners.requestPaint()
    onPersonTopChanged: corners.requestPaint()
    onPersonRightChanged: corners.requestPaint()
    onPersonBottomChanged: corners.requestPaint()
    onMarkedChanged: corners.requestPaint()
}
