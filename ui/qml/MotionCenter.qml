import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import PfUi
import PfUiBridge

Column {
    id: root
    focus: true
    activeFocusOnTab: true
    property var sourceFiles: []
    property var selectedRecord: null
    property real timelineZoom: 1.0
    property real timelineOffset: 0.0
    property bool fullscreenTimeline: false
    property bool analysisCompleted: !Analysis.busy && Analysis.progress >= 0.999 && String(Analysis.status).indexOf("Анализ ") === 0
    signal addRequested()
    signal analyzeRequested()

    onSelectedRecordChanged: {
        root.timelineZoom = 1.0
        root.timelineOffset = 0.0
        root.fullscreenTimeline = false
    }

    Keys.onEscapePressed: if (root.fullscreenTimeline) root.fullscreenTimeline = false

    function setZoom(nextZoom) {
        const oldZoom = root.timelineZoom
        root.timelineZoom = Math.max(1, Math.min(8, nextZoom))
        if (root.timelineZoom <= 1) root.timelineOffset = 0
        else root.timelineOffset = Math.max(0, Math.min(1, root.timelineOffset + (oldZoom - root.timelineZoom) * 0.08))
    }

    function markerX(value, width) {
        const duration = Math.max(0.001, Number(root.selectedRecord && root.selectedRecord.duration) || 0.001)
        const normalized = Math.max(0, Math.min(1, Number(value) / duration))
        return normalized * width * root.timelineZoom
    }
    spacing: 12

    function timecode(seconds) {
        const total = Math.max(0, Number(seconds) || 0)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = Math.floor(total % 60)
        return [h, m, s].map(function (v) { return String(v).padStart(2, "0") }).join(":")
    }
    function fileName(path) {
        const parts = String(path || "").replace(/\\/g, "/").split("/")
        return parts[parts.length - 1] || L10n.t("common.empty")
    }

    StatsStrip { width: parent.width; analysis: Analysis }
    Rectangle {
        width: parent.width; height: parent.height - 80; color: Theme.panel; radius: Theme.radiusCard; border.color: Theme.border
        layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Theme.shadowPanel; shadowBlur: 0.72; shadowVerticalOffset: 12 }
        Column { anchors.fill: parent; anchors.margins: 16; spacing: 11
            Row { width: parent.width; height: 23
                Text { text: L10n.t("center.comparison"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                Item { width: parent.width - 310; height: 1 }
                Text { text: Analysis.busy ? Analysis.progressStage : (root.selectedRecord ? L10n.t("center.selected") : L10n.t("center.waiting")); color: Theme.textSecondary; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
            }
            Rectangle { width: parent.width; height: 5; radius: 3; color: Theme.well
                Rectangle { width: parent.width * Analysis.progress; height: parent.height; radius: 3; color: Theme.accent }
            }
            Rectangle {
                id: stage; width: parent.width; height: parent.height - 102; color: Theme.well; radius: Theme.radiusButton; border.color: Theme.hairline; clip: true
                Column { anchors.fill: parent; anchors.margins: 16; spacing: 10; visible: !root.selectedRecord
                    Row { width: parent.width
                        Text { text: L10n.t("center.motionField"); color: Theme.textDisabled; font.pixelSize: 10; font.letterSpacing: 0.8 }
                        Item { width: parent.width - 260; height: 1 }
                        Text { text: Analysis.fileCount > 0 ? Analysis.poseDetectionCount + " " + L10n.t("center.poseCount") : L10n.t("center.noMaterial"); color: Theme.textSecondary; font.pixelSize: 10 }
                    }
                    Item { width: parent.width; height: parent.height - 72
                        Column { anchors.centerIn: parent; spacing: 9
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sourceFiles.length === 0 ? L10n.t("center.emptyTitle") : root.analysisCompleted ? (Analysis.matchCount > 0 ? L10n.t("center.completedTitle") : L10n.t("center.noMatchesTitle")) : L10n.t("center.readyTitle"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 20 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sourceFiles.length === 0 ? L10n.t("center.emptyHint") : root.analysisCompleted ? (Analysis.matchCount > 0 ? L10n.t("center.completedHint") : L10n.t("center.noMatchesHint")) : L10n.t("center.readyHint"); color: Theme.textSecondary; font.pixelSize: 12 }
                            PfButton { anchors.horizontalCenter: parent.horizontalCenter; visible: root.sourceFiles.length === 0; text: L10n.t("sources.add"); onClicked: root.addRequested() }
                            PfButton { anchors.horizontalCenter: parent.horizontalCenter; visible: root.sourceFiles.length > 0 && !Analysis.busy; text: root.analysisCompleted ? L10n.t("search.restart") : L10n.t("search.start"); onClicked: root.analyzeRequested() }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; visible: Analysis.busy; text: Analysis.progressStage + " · " + Math.round(Analysis.progress * 100) + "%"; color: Theme.accent; font.pixelSize: 11 }
                        }
                    }
                }
                Row { anchors.fill: parent; anchors.margins: 12; spacing: 10; visible: !!root.selectedRecord
                    Rectangle { width: (parent.width - 10) / 2; height: parent.height; color: Theme.canvas; radius: Theme.radiusButton; border.color: Theme.accent
                        Column { anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Row { width: parent.width
                                Text { text: L10n.t("timeline.left"); color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 17 }
                                Item { width: parent.width - 38; height: 1 }
                                Text { text: root.timecode(root.selectedRecord ? root.selectedRecord.leftStart : 0); color: Theme.textSecondary; font.pixelSize: 10 }
                            }
                            Rectangle { width: parent.width; height: parent.height - 42; color: Theme.well; radius: 6
                                Image { anchors.fill: parent; anchors.margins: 6; source: root.selectedRecord ? (root.selectedRecord.leftPreview || "") : ""; fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true; visible: source.length > 0 }
                                Text { anchors.centerIn: parent; visible: !root.selectedRecord || root.selectedRecord.leftPreview === undefined || !root.selectedRecord.leftPreview; text: L10n.t("timeline.left"); color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 42 }
                            }
                            Text { text: root.fileName(root.selectedRecord ? root.selectedRecord.leftSource : ""); color: Theme.textDisabled; font.pixelSize: 9; elide: Text.ElideMiddle; width: parent.width }
                        }
                    }
                    Rectangle { width: (parent.width - 10) / 2; height: parent.height; color: Theme.canvas; radius: Theme.radiusButton; border.color: Theme.sage
                        Column { anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Row { width: parent.width
                                Text { text: L10n.t("timeline.right"); color: Theme.sage; font.family: Theme.displayFont; font.pixelSize: 17 }
                                Item { width: parent.width - 38; height: 1 }
                                Text { text: root.timecode(root.selectedRecord ? root.selectedRecord.rightStart : 0); color: Theme.textSecondary; font.pixelSize: 10 }
                            }
                            Rectangle { width: parent.width; height: parent.height - 42; color: Theme.well; radius: 6
                                Image { anchors.fill: parent; anchors.margins: 6; source: root.selectedRecord ? (root.selectedRecord.rightPreview || "") : ""; fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true; visible: source.length > 0 }
                                Text { anchors.centerIn: parent; visible: !root.selectedRecord || root.selectedRecord.rightPreview === undefined || !root.selectedRecord.rightPreview; text: L10n.t("timeline.right"); color: Theme.sage; font.family: Theme.displayFont; font.pixelSize: 42 }
                            }
                            Text { text: root.fileName(root.selectedRecord ? root.selectedRecord.rightSource : ""); color: Theme.textDisabled; font.pixelSize: 9; elide: Text.ElideMiddle; width: parent.width }
                        }
                    }
                }
            }
            Row { width: parent.width; height: 24
                Text { text: L10n.t("timeline.title"); color: Theme.textDisabled; font.pixelSize: 10; font.letterSpacing: 0.8 }
                Item { width: parent.width - 300; height: 1 }
                Text { visible: !!root.selectedRecord; text: root.selectedRecord ? root.timecode(root.selectedRecord.duration) : L10n.t("common.empty"); color: Theme.textSecondary; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter }
                PfIconButton { width: 24; height: 24; iconSource: "qrc:/qt/qml/PfUi/qml/assets/minus.svg"; accessibleName: L10n.t("timeline.zoomOut"); enabled: !!root.selectedRecord && root.timelineZoom > 1; onClicked: root.setZoom(root.timelineZoom - 1) }
                PfIconButton { width: 24; height: 24; iconSource: "qrc:/qt/qml/PfUi/qml/assets/plus.svg"; accessibleName: L10n.t("timeline.zoomIn"); enabled: !!root.selectedRecord && root.timelineZoom < 8; onClicked: root.setZoom(root.timelineZoom + 1) }
                PfIconButton { width: 24; height: 24; iconSource: "qrc:/qt/qml/PfUi/qml/assets/maximize.svg"; accessibleName: L10n.t("timeline.fullscreen"); enabled: !!root.selectedRecord; onClicked: { root.fullscreenTimeline = true; root.forceActiveFocus() } }
            }
            Rectangle { id: timelineTrack; width: parent.width; height: 42; color: Theme.surfaceRaised; radius: 5; border.color: Theme.border; clip: true
                Item { id: timelineContent; x: -root.timelineOffset * Math.max(0, width * root.timelineZoom - timelineTrack.width); width: timelineTrack.width * root.timelineZoom; height: parent.height
                    Rectangle { x: 8; y: 20; width: Math.max(20, parent.width - 16); height: 2; radius: 1; color: Theme.panelAlt }
                    Repeater { model: root.selectedRecord ? root.selectedRecord.markers : []; delegate: Rectangle { x: root.markerX(modelData, timelineTrack.width); y: 7; width: 2; height: 28; color: index < 2 ? Theme.accent : Theme.sage; layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: index < 2 ? Theme.accent : Theme.sage; shadowBlur: 0.55 } } }
                    MouseArea { anchors.fill: parent; enabled: root.timelineZoom > 1; cursorShape: Qt.OpenHandCursor; property real pressX; property real initialOffset
                        onPressed: { pressX = mouse.x; initialOffset = root.timelineOffset; cursorShape = Qt.ClosedHandCursor }
                        onReleased: cursorShape = Qt.OpenHandCursor
                        onPositionChanged: if (pressed) { const travel = Math.max(1, timelineContent.width - timelineTrack.width); root.timelineOffset = Math.max(0, Math.min(1, initialOffset - (mouse.x - pressX) / travel)) }
                    }
                }
                Text { anchors.centerIn: parent; visible: !root.selectedRecord; text: L10n.t("common.empty"); color: Theme.textDisabled; font.pixelSize: 12 }
            }
        }
    }

    Rectangle { id: fullscreenOverlay; anchors.fill: parent; visible: root.fullscreenTimeline && !!root.selectedRecord; z: 30; color: Theme.canvas; border.color: Theme.hairlineStrong; radius: Theme.radiusOverlay; focus: visible; Keys.onEscapePressed: root.fullscreenTimeline = false
        Column { anchors.fill: parent; anchors.margins: 24; spacing: 16
            Row { width: parent.width; height: 34
                Text { text: L10n.t("center.comparison"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 22; verticalAlignment: Text.AlignVCenter }
                Item { width: parent.width - 80; height: 1 }
                PfIconButton { iconSource: "qrc:/qt/qml/PfUi/qml/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: root.fullscreenTimeline = false }
            }
            Row { width: parent.width; height: parent.height - 112; spacing: 14
                Rectangle { width: (parent.width - 14) / 2; height: parent.height; color: Theme.well; radius: Theme.radiusButton; border.color: Theme.accent
                    Image { anchors.fill: parent; anchors.margins: 10; source: root.selectedRecord ? (root.selectedRecord.leftPreview || "") : ""; fillMode: Image.PreserveAspectFit; smooth: true }
                    Text { anchors.centerIn: parent; visible: !root.selectedRecord || !root.selectedRecord.leftPreview; text: L10n.t("timeline.left"); color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 48 }
                }
                Rectangle { width: (parent.width - 14) / 2; height: parent.height; color: Theme.well; radius: Theme.radiusButton; border.color: Theme.sage
                    Image { anchors.fill: parent; anchors.margins: 10; source: root.selectedRecord ? (root.selectedRecord.rightPreview || "") : ""; fillMode: Image.PreserveAspectFit; smooth: true }
                    Text { anchors.centerIn: parent; visible: !root.selectedRecord || !root.selectedRecord.rightPreview; text: L10n.t("timeline.right"); color: Theme.sage; font.family: Theme.displayFont; font.pixelSize: 48 }
                }
            }
            Rectangle { width: parent.width; height: 44; color: Theme.surfaceRaised; radius: 6; border.color: Theme.border; clip: true
                Item { x: -root.timelineOffset * Math.max(0, width * root.timelineZoom - parent.width); width: parent.width * root.timelineZoom; height: parent.height
                    Repeater { model: root.selectedRecord ? root.selectedRecord.markers : []; delegate: Rectangle { x: root.markerX(modelData, fullscreenOverlay.width - 48); y: 7; width: 2; height: 30; color: index < 2 ? Theme.accent : Theme.sage } }
                }
            }
        }
    }
}
