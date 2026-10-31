import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PfUi

Popup {
    id: root
    objectName: "sceneFamilyInspector"
    property var record: null
    property string activeSide: "left"
    property var previewRecord: null
    property bool previewFailed: false
    readonly property var sideChoices: ["left","right"].filter(function(side) {
        const family=record && record[side+"Family"]
        return family && family.members && family.members.length && !record[side+"Removed"]
    })
    readonly property var currentFamily: record ? record[activeSide+"Family"] || null : null
    readonly property real previewHeight: Math.min(220,parent ? parent.height*.26 : 220)
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(660,parent ? parent.width-32 : 660)
    height: Math.min(parent ? parent.height-32 : 660,200+Math.min(5,currentFamily && currentFamily.members ? currentFamily.members.length : 0)*90+(previewRecord ? previewHeight : 0))
    Behavior on height { enabled: root.opened && !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.InOutCubic } }
    modal: true; focus: true; padding: 16
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.heroPanel; radius: Theme.radiusOverlay; border.color: Theme.hairlineStrong }
    onOpened: { activeSide=sideChoices[0] || "left"; memberList.positionViewAtBeginning() }
    onRecordChanged: if (opened) close()
    onActiveSideChanged: { preview.stopPlayback(); previewRecord=null; previewFailed=false; memberList.positionViewAtBeginning() }
    onClosed: { preview.stopPlayback(); previewRecord=null; previewFailed=false }
    enter: Transition { enabled: !Theme.reducedMotion; ParallelAnimation {
        NumberAnimation { property: "scale"; from: Theme.motionPopupScale; to: 1; duration: Theme.motionPopupDuration; easing.type: Easing.OutCubic }
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionChangeDuration }
    } }
    exit: Transition { enabled: !Theme.reducedMotion; ParallelAnimation {
        NumberAnimation { property: "scale"; to: Theme.motionPopupScale; duration: Theme.motionChangeDuration; easing.type: Easing.InCubic }
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionChangeDuration }
    } }
    function basename(path) { return String(path || "").replace(/\\/g,"/").split("/").pop() }
    function rangeText(start,end) { return L10n.t("character.time").arg(Number(start).toFixed(2)+"–"+Number(end).toFixed(2)) }
    function reasonLabel(reason) {
        const key=reason === "representative" ? "selected" : reason === "overlapping_event" ? "overlap"
            : reason === "adjacent_event" ? "adjacent" : reason === "copied_footage" ? "copy" : "unknown"
        return L10n.t("family."+key)
    }
    function inspect(member) {
        if(!currentFamily || !member)return
        previewFailed=false
        const fps=Number(currentFamily.frameRate)>0 ? Number(currentFamily.frameRate) : 24
        let end=Number(member.end)+.5/fps
        if(Number(member.sceneEnd)>Number(member.start))end=Math.min(end,Number(member.sceneEnd))
        previewRecord={leftSource:currentFamily.source,leftStart:member.start,leftEnd:member.end,
            leftClipStart:member.start,leftClipEnd:end,leftSceneStart:member.start,leftSceneEnd:end,matchType:"motion"}
        Qt.callLater(function() { if(root.opened && root.previewRecord)preview.preparePlayback() })
    }
    contentItem: ColumnLayout {
        spacing: 8
        Text { Layout.fillWidth: true; text: L10n.t("family.title"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 18; font.weight: Font.DemiBold }
        Text { Layout.fillWidth: true; visible: !root.previewRecord; text: L10n.t("family.hint"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
        PfComboBox {
            objectName: "familySideSelector"; Layout.fillWidth: true
            model: root.sideChoices.map(function(side) { return (side === "left" ? "A · " : "B · ")+root.basename(root.record[side+"Family"].source) })
            currentIndex: Math.max(0,root.sideChoices.indexOf(root.activeSide))
            onActivated: function(index) { root.activeSide=root.sideChoices[index] }
            Accessible.name: L10n.t("family.title")
        }
        Item {
            property real expandedHeight: root.previewRecord ? root.previewHeight : 0
            Behavior on expandedHeight { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.motionChangeDuration; easing.type: Easing.InOutCubic } }
            Layout.fillWidth: true; Layout.preferredHeight: expandedHeight; Layout.minimumHeight: 0
            visible: expandedHeight>0; clip: true
            ComparisonView {
                id: preview; objectName: "familyMemberPreview"; anchors.fill: parent
                record: root.previewRecord; side: "left"; title: root.activeSide === "left" ? "A" : "B"
                onPreviewRequested: {
                    if (player && playbackReady && player.position>=playbackEnd*1000)player.position=playbackStart*1000
                    togglePlayback()
                }
                onPlaybackFailed: function(message) { root.previewFailed=true; console.warn("FAMILY_PREVIEW_FAILED",message) }
            }
        }
        Text { Layout.fillWidth: true; visible: root.previewFailed; text: L10n.t("family.previewFailed"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap }
        ListView {
            id: memberList; objectName: "familyMemberList"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 40
            clip: true; spacing: 6; boundsBehavior: Flickable.StopAtBounds
            model: root.currentFamily ? root.currentFamily.members : []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            header: Text {
                width: memberList.width; height: implicitHeight+10
                text: L10n.t("family.count").arg(memberList.count); color: Theme.textSecondary
                font.family: Theme.fontFamily; font.pixelSize: 11
            }
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: 84; radius: Theme.radiusButton
                color: modelData.representative ? Theme.accentMuted : Theme.panelAlt
                border.color: modelData.representative ? Theme.accent : Theme.hairlineStrong
                RowLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 3
                        Text { Layout.fillWidth: true; text: root.rangeText(modelData.start,modelData.end); color: Theme.textPrimary; font.family: Theme.monoFont; font.pixelSize: 11 }
                        Text { Layout.fillWidth: true; text: root.reasonLabel(modelData.reason); color: modelData.representative ? Theme.accentText : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                        Text { text: L10n.t("family.quality")+" · "+Math.round(Number(modelData.quality || 0)*100)+"%"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: 10 }
                    }
                    PfButton { objectName: "inspectFamilyMember"; compact: true; quiet: true; text: L10n.t("family.watch"); onClicked: root.inspect(modelData) }
                }
            }
        }
        PfButton { objectName: "closeSceneFamilyInspector"; Layout.alignment: Qt.AlignRight; compact: true; text: L10n.t("explain.close"); onClicked: root.close() }
    }
}
