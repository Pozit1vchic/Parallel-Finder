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
    property real auraX: 0.5
    property real auraY: 0.5
    signal addRequested()

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
        layer.enabled: true; layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.50); shadowBlur: 0.72; shadowVerticalOffset: 12 }
        Column { anchors.fill: parent; anchors.margins: 16; spacing: 11
            Row { width: parent.width; height: 23
                Text { text: L10n.t("center.comparison"); color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                Item { width: parent.width - 310; height: 1 }
                Text { text: Analysis.busy ? Analysis.progressStage : (root.selectedRecord ? L10n.t("center.selected") : L10n.t("center.waiting")); color: Theme.textSecondary; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
            }
            Rectangle { width: parent.width; height: 5; radius: 3; color: Theme.well
                Rectangle { width: parent.width * Analysis.progress; height: parent.height; radius: 3; color: Theme.accent; Behavior on width { NumberAnimation { duration: Theme.motionDuration } } }
            }
            Rectangle {
                id: stage; width: parent.width; height: parent.height - 102; color: Theme.well; radius: Theme.radiusButton; border.color: Theme.hairline
                MouseArea { anchors.fill: parent; z: 0; hoverEnabled: true; acceptedButtons: Qt.NoButton
                    onPositionChanged: { root.auraX = Math.max(0, Math.min(1, mouse.x / Math.max(1, width))); root.auraY = Math.max(0, Math.min(1, mouse.y / Math.max(1, height))) }
                }
                MotionAura { z: 0; width: 430; height: 300; x: root.auraX * stage.width - width / 2; y: root.auraY * stage.height - height / 2; targetX: root.auraX; targetY: root.auraY; active: !root.selectedRecord }
                Column { anchors.fill: parent; anchors.margins: 16; spacing: 10; visible: !root.selectedRecord
                    Row { width: parent.width
                        Text { text: L10n.t("center.motionField"); color: Theme.textDisabled; font.pixelSize: 10; font.letterSpacing: 0.8 }
                        Item { width: parent.width - 260; height: 1 }
                        Text { text: Analysis.fileCount > 0 ? Analysis.poseDetectionCount + " " + L10n.t("center.poseCount") : L10n.t("center.noMaterial"); color: Theme.textSecondary; font.pixelSize: 10 }
                    }
                    Item { width: parent.width; height: parent.height - 72
                        Rectangle { anchors.centerIn: parent; width: 260; height: 160; radius: 80; color: Theme.glowA; opacity: 0.10; layer.enabled: true; layer.effect: MultiEffect { blurEnabled: true; blur: 1.0 } }
                        Canvas { anchors.centerIn: parent; width: Math.min(520, parent.width - 40); height: 190
                            onPaint: {
                                const ctx = getContext("2d"); ctx.clearRect(0, 0, width, height); const cy = height * 0.54; ctx.lineCap = "round"
                                ctx.lineWidth = 8; ctx.globalAlpha = 0.05; ctx.strokeStyle = Theme.accent; ctx.beginPath(); ctx.moveTo(26, cy + 10); ctx.bezierCurveTo(width * 0.30, 20, width * 0.64, height - 6, width - 24, 46); ctx.stroke()
                                ctx.lineWidth = 1; ctx.globalAlpha = 0.76; ctx.strokeStyle = Theme.accent; ctx.beginPath(); ctx.moveTo(26, cy + 10); ctx.bezierCurveTo(width * 0.30, 20, width * 0.64, height - 6, width - 24, 46); ctx.stroke()
                                ctx.lineWidth = 8; ctx.globalAlpha = 0.045; ctx.strokeStyle = Theme.sage; ctx.beginPath(); ctx.moveTo(40, 44); ctx.bezierCurveTo(width * 0.34, height - 8, width * 0.68, 30, width - 36, cy + 18); ctx.stroke()
                                ctx.lineWidth = 1; ctx.globalAlpha = 0.68; ctx.strokeStyle = Theme.sage; ctx.beginPath(); ctx.moveTo(40, 44); ctx.bezierCurveTo(width * 0.34, height - 8, width * 0.68, 30, width - 36, cy + 18); ctx.stroke()
                                ctx.globalAlpha = 0.95; ctx.fillStyle = Theme.accent; ctx.beginPath(); ctx.arc(26, cy + 10, 3, 0, Math.PI * 2); ctx.fill(); ctx.fillStyle = Theme.sage; ctx.beginPath(); ctx.arc(width - 36, cy + 18, 3, 0, Math.PI * 2); ctx.fill()
                            }
                        }
                        Column { anchors.centerIn: parent; spacing: 9
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sourceFiles.length > 0 ? L10n.t("center.readyTitle") : L10n.t("center.emptyTitle"); color: Theme.textPrimary; font.family: Theme.displayFont; font.pixelSize: 20 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sourceFiles.length > 0 ? L10n.t("center.readyHint") : L10n.t("center.emptyHint"); color: Theme.textSecondary; font.pixelSize: 12 }
                            PfButton { anchors.horizontalCenter: parent.horizontalCenter; visible: root.sourceFiles.length === 0; text: L10n.t("sources.add"); onClicked: root.addRequested() }
                        }
                    }
                }
                Row { anchors.fill: parent; anchors.margins: 12; spacing: 10; visible: !!root.selectedRecord
                    Rectangle { width: (parent.width - 10) / 2; height: parent.height; color: Theme.canvas; radius: Theme.radiusButton; border.color: Theme.accent
                        Column { anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Row { width: parent.width
                                Text { text: L10n.t("timeline.left"); color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 17 }
                                Item { width: parent.width - 38; height: 1 }
                                Text { text: root.timecode(root.selectedRecord.leftStart); color: Theme.textSecondary; font.pixelSize: 10 }
                            }
                            Rectangle { width: parent.width; height: parent.height - 42; color: Theme.well; radius: 6
                                Image { anchors.fill: parent; source: root.selectedRecord.leftPreview || ""; fillMode: Image.PreserveAspectCrop; smooth: true; visible: source.length > 0 }
                                Text { anchors.centerIn: parent; visible: root.selectedRecord.leftPreview === undefined || !root.selectedRecord.leftPreview; text: L10n.t("timeline.left"); color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 42 }
                            }
                            Text { text: root.fileName(root.selectedRecord.leftSource); color: Theme.textDisabled; font.pixelSize: 9; elide: Text.ElideMiddle; width: parent.width }
                        }
                    }
                    Rectangle { width: (parent.width - 10) / 2; height: parent.height; color: Theme.canvas; radius: Theme.radiusButton; border.color: Theme.sage
                        Column { anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Row { width: parent.width
                                Text { text: L10n.t("timeline.right"); color: Theme.sage; font.family: Theme.displayFont; font.pixelSize: 17 }
                                Item { width: parent.width - 38; height: 1 }
                                Text { text: root.timecode(root.selectedRecord.rightStart); color: Theme.textSecondary; font.pixelSize: 10 }
                            }
                            Rectangle { width: parent.width; height: parent.height - 42; color: Theme.well; radius: 6
                                Image { anchors.fill: parent; source: root.selectedRecord.rightPreview || ""; fillMode: Image.PreserveAspectCrop; smooth: true; visible: source.length > 0 }
                                Text { anchors.centerIn: parent; visible: root.selectedRecord.rightPreview === undefined || !root.selectedRecord.rightPreview; text: L10n.t("timeline.right"); color: Theme.sage; font.family: Theme.displayFont; font.pixelSize: 42 }
                            }
                            Text { text: root.fileName(root.selectedRecord.rightSource); color: Theme.textDisabled; font.pixelSize: 9; elide: Text.ElideMiddle; width: parent.width }
                        }
                    }
                }
            }
            Row { width: parent.width; height: 24
                Text { text: L10n.t("timeline.title"); color: Theme.textDisabled; font.pixelSize: 10; font.letterSpacing: 0.8 }
                Item { width: parent.width - 300; height: 1 }
                Text { visible: !!root.selectedRecord; text: root.selectedRecord ? root.timecode(root.selectedRecord.duration) : L10n.t("common.empty"); color: Theme.textSecondary; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter }
                PfIconButton { width: 24; height: 24; iconSource: "qrc:/qt/qml/PfUi/assets/minus.svg"; accessibleName: "Уменьшить масштаб таймлайна"; enabled: !!root.selectedRecord && root.timelineZoom > 1; onClicked: root.setZoom(root.timelineZoom - 1) }
                PfIconButton { width: 24; height: 24; iconSource: "qrc:/qt/qml/PfUi/assets/plus.svg"; accessibleName: "Увеличить масштаб таймлайна"; enabled: !!root.selectedRecord && root.timelineZoom < 8; onClicked: root.setZoom(root.timelineZoom + 1) }
                PfIconButton { width: 24; height: 24; iconSource: "qrc:/qt/qml/PfUi/assets/maximize.svg"; accessibleName: "Открыть таймлайн на весь экран"; enabled: !!root.selectedRecord; onClicked: { root.fullscreenTimeline = true; root.forceActiveFocus() } }
            }
            Rectangle { id: timelineTrack; width: parent.width; height: 42; color: Theme.surfaceRaised; radius: 5; border.color: Theme.border; clip: true
                Item { id: timelineContent; x: -root.timelineOffset * Math.max(0, width * root.timelineZoom - timelineTrack.width); width: timelineTrack.width * root.timelineZoom; height: parent.height
                    Rectangle { x: 8; y: 13; width: Math.max(20, parent.width * 0.18); height: 16; radius: 3; color: Theme.sageMuted }
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
                PfIconButton { iconSource: "qrc:/qt/qml/PfUi/assets/x.svg"; accessibleName: L10n.t("common.close"); onClicked: root.fullscreenTimeline = false }
            }
            Row { width: parent.width; height: parent.height - 112; spacing: 14
                Rectangle { width: (parent.width - 14) / 2; height: parent.height; color: Theme.well; radius: Theme.radiusButton; border.color: Theme.accent
                    Image { anchors.fill: parent; anchors.margins: 10; source: root.selectedRecord.leftPreview || ""; fillMode: Image.PreserveAspectFit; smooth: true }
                    Text { anchors.centerIn: parent; visible: !root.selectedRecord.leftPreview; text: L10n.t("timeline.left"); color: Theme.accent; font.family: Theme.displayFont; font.pixelSize: 48 }
                }
                Rectangle { width: (parent.width - 14) / 2; height: parent.height; color: Theme.well; radius: Theme.radiusButton; border.color: Theme.sage
                    Image { anchors.fill: parent; anchors.margins: 10; source: root.selectedRecord.rightPreview || ""; fillMode: Image.PreserveAspectFit; smooth: true }
                    Text { anchors.centerIn: parent; visible: !root.selectedRecord.rightPreview; text: L10n.t("timeline.right"); color: Theme.sage; font.family: Theme.displayFont; font.pixelSize: 48 }
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
