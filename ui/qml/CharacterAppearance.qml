import QtQuick
import PfUi

Item {
    id: root
    property alias source: frame.source
    property var personBox: null
    readonly property bool marked: personBox && personBox.right > personBox.left && personBox.bottom > personBox.top
    readonly property real imageLeft: (width-frame.paintedWidth)/2
    readonly property real imageTop: (height-frame.paintedHeight)/2
    readonly property real personLeft: imageLeft + (marked ? personBox.left : 0)*frame.paintedWidth
    readonly property real personTop: imageTop + (marked ? personBox.top : 0)*frame.paintedHeight
    readonly property real personRight: imageLeft + (marked ? personBox.right : 1)*frame.paintedWidth
    readonly property real personBottom: imageTop + (marked ? personBox.bottom : 1)*frame.paintedHeight
    clip: true
    Image { id: frame; anchors.fill: parent; fillMode: Image.PreserveAspectFit; asynchronous: true }
    // Leave the target untouched; quiet shading separates overlapping people.
    Item {
        visible: root.marked
        anchors.fill: parent
        Rectangle { x: root.imageLeft; y: root.imageTop; width: frame.paintedWidth; height: Math.max(0,root.personTop-y); color: "#30000000" }
        Rectangle { x: root.imageLeft; y: root.personBottom; width: frame.paintedWidth; height: Math.max(0,root.imageTop+frame.paintedHeight-y); color: "#30000000" }
        Rectangle { x: root.imageLeft; y: root.personTop; width: Math.max(0,root.personLeft-x); height: root.personBottom-y; color: "#30000000" }
        Rectangle { x: root.personRight; y: root.personTop; width: Math.max(0,root.imageLeft+frame.paintedWidth-x); height: root.personBottom-y; color: "#30000000" }
    }
    Canvas {
        id: corners
        anchors.fill: parent
        visible: root.marked
        property color markerColor: Theme.accent
        onMarkerColorChanged: requestPaint()
        onPaint: {
            const ctx=getContext("2d");ctx.reset()
            if(!root.marked)return
            const l=Math.max(2,root.personLeft),t=Math.max(2,root.personTop)
            const r=Math.min(width-2,root.personRight),b=Math.min(height-2,root.personBottom)
            const length=Math.min(14,(r-l)*.22,(b-t)*.22),radius=Math.min(5,length*.45)
            ctx.strokeStyle=markerColor;ctx.lineWidth=1.6;ctx.lineCap="round";ctx.lineJoin="round"
            function corner(x,y,h,v) {
                ctx.beginPath();ctx.moveTo(x,y+v*length);ctx.lineTo(x,y+v*radius)
                ctx.quadraticCurveTo(x,y,x+h*radius,y);ctx.lineTo(x+h*length,y);ctx.stroke()
            }
            corner(l,t,1,1);corner(r,t,-1,1);corner(l,b,1,-1);corner(r,b,-1,-1)
        }
    }
    onPersonLeftChanged: corners.requestPaint()
    onPersonTopChanged: corners.requestPaint()
    onPersonRightChanged: corners.requestPaint()
    onPersonBottomChanged: corners.requestPaint()
    onMarkedChanged: corners.requestPaint()
}
