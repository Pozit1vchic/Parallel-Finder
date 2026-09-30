import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import PfUi
import PfUiBridge

// The center is intentionally a viewport, not a dashboard. Once a pair is
// selected the A/B surfaces take all available height; the old timeline strip
// was removed because it duplicated result timestamps and stole preview area.
ColumnLayout {
    id: root
    focus: true
    activeFocusOnTab: true
    property var sourceFiles: []
    property var selectedRecord: null
    property bool analysisCompleted: Analysis.analysisCompleted && !Analysis.busy
    property bool pendingPlayback: false
    property bool pairedPlayback: false
    property string playbackError: ""
    onSelectedRecordChanged: stopPair()
    function stopPair() {
        pendingPlayback = false
        pairedPlayback = false
        leftView.stopPlayback()
        rightView.stopPlayback()
    }
    function startPair() {
        stopPair()
        playbackError = ""
        if (!leftView.previewPlayable || !rightView.previewPlayable) return
        pendingPlayback = true
        pairedPlayback = true
        leftView.preparePlayback()
        rightView.preparePlayback()
        tryPlayPair()
    }
    function tryPlayPair() {
        if (pendingPlayback && leftView.playbackReady && rightView.playbackReady) {
            pendingPlayback = false
            leftView.playPrepared()
            rightView.playPrepared()
        }
    }
    function togglePair() {
        if (!pairedPlayback) startPair()
        else if (leftView.playing || rightView.playing) {
            leftView.pausePlayback(); rightView.pausePlayback()
        } else if (leftView.videoMode && rightView.videoMode && !pendingPlayback) {
            leftView.playPrepared(); rightView.playPrepared()
        } else startPair()
    }
    function toggleSingle(view) {
        // Detach the clocks without resetting either player. In particular,
        // a panel button labelled Pause must not restart that clip.
        pendingPlayback = false
        pairedPlayback = false
        playbackError = ""
        view.togglePlayback()
    }
    function finishPlayback(view) {
        if (pairedPlayback) stopPair()
        else view.stopPlayback()
    }
    signal addRequested()
    signal analyzeRequested()
    spacing: 12

    StatsStrip { Layout.fillWidth: true; Layout.preferredHeight: 68; analysis: Analysis }

    Rectangle {
        id: comparisonPanel
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 220
        color: Theme.panel
        radius: Theme.radiusCard
        border.color: Theme.border
        clip: true

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Item {
                width: parent.width
                height: 24
                Text { font.family: Theme.fontFamily;
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: L10n.t("center.comparison")
                    color: Theme.textPrimary
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }
                Text { font.family: Theme.fontFamily;
                    id: stateText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(parent.width * 0.58, implicitWidth)
                    text: Analysis.busy
                        ? L10n.status(Analysis.progressStage)
                        : root.selectedRecord
                            ? (L10n.matchLabel(root.selectedRecord) + " · " + Math.round(Number(root.selectedRecord.similarity || 0) * 100) + "%"
                                + "  •  " + L10n.t("results.sceneSimilarity") + " · " + Math.round(Number(root.selectedRecord.sceneSimilarity || 0) * 100) + "%")
                            : root.analysisCompleted
                                ? (Analysis.matchCount > 0 ? L10n.t("center.completedTitle") : L10n.t("center.noMatchesStatus"))
                                : root.sourceFiles.length > 0 ? L10n.t("center.readyStatus") : L10n.t("center.waiting")
                    color: Analysis.busy ? Theme.accent : Theme.textSecondary
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }

            Item {
                width: parent.width
                height: (Analysis.busy || root.analysisCompleted) ? 20 : 0
                visible: Analysis.busy || root.analysisCompleted
                Text { font.family: Theme.fontFamily;
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: L10n.t("stats.progress")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                }
                Text { font.family: Theme.fontFamily;
                    id: progressText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round(Analysis.progress * 100) + "%"
                    color: Analysis.busy ? Theme.accent : Theme.textSecondary
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                }
            }

            Rectangle {
                width: parent.width
                height: (Analysis.busy || root.analysisCompleted) ? 8 : 0
                visible: Analysis.busy || root.analysisCompleted
                radius: 4
                color: Theme.surfaceMuted
                border.width: 1
                border.color: Theme.hairline
                Accessible.role: Accessible.ProgressBar
                Accessible.name: L10n.t("stats.progress") + ": " + Math.round(Analysis.progress * 100) + "%"
                Rectangle {
                    width: Math.max(0, parent.width * Math.max(0, Math.min(1, Analysis.progress)))
                    height: parent.height - 2
                    y: 1
                    radius: 3
                    color: Analysis.busy ? Theme.accent : Theme.sage
                    Behavior on width { enabled: !Theme.reducedMotion && Analysis.progress > 0; NumberAnimation { duration: Theme.motionRevealDuration; easing.type: Easing.OutCubic } }
                    Behavior on color { enabled: !Theme.reducedMotion; ColorAnimation { duration: Theme.motionChangeDuration } }
                }
            }

            Rectangle {
                id: stage
                width: parent.width
                height: Math.max(220, parent.height - ((Analysis.busy || root.analysisCompleted) ? 78 : 50))
                color: Theme.well
                radius: Theme.radiusButton
                border.color: Theme.hairline
                clip: true

                Item {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: !!root.selectedRecord
                    Row {
                        id: playbackControls
                        anchors.bottom: parent.bottom
                        height: visible ? 36 : 0
                        spacing: 8
                        visible: !!root.selectedRecord
                        PfButton {
                            objectName: "pairPlaybackButton"
                            compact: true
                            text: root.pendingPlayback ? L10n.t("timeline.loadingFrame") : (root.pairedPlayback && (leftView.playing || rightView.playing) ? L10n.t("preview.pause") : L10n.t("preview.playPair"))
                            enabled: !root.pendingPlayback
                                     && leftView.previewPlayable && rightView.previewPlayable
                            onClicked: root.togglePair()
                        }
                        Text { text: root.playbackError; color: Theme.textSecondary; width: Math.max(0, stage.width - 330); elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: playbackControls.top
                        anchors.bottomMargin: playbackControls.visible ? 8 : 0
                        spacing: 12
                        ComparisonView {
                            id: leftView
                            objectName: "leftComparison"
                            onPreviewRequested: root.toggleSingle(leftView)
                            onPlaybackReadyChanged: root.tryPlayPair()
                            onPlaybackFinished: root.finishPlayback(leftView)
                            onPlaybackFailed: function(message) { root.playbackError = message; root.finishPlayback(leftView) }
                            width: (parent.width - 12) / 2
                            height: parent.height
                            record: root.selectedRecord
                            side: "left"
                            accentColor: Theme.accent
                        }
                        ComparisonView {
                            id: rightView
                            objectName: "rightComparison"
                            onPreviewRequested: root.toggleSingle(rightView)
                            onPlaybackReadyChanged: root.tryPlayPair()
                            onPlaybackFinished: root.finishPlayback(rightView)
                            onPlaybackFailed: function(message) { root.playbackError = message; root.finishPlayback(rightView) }
                            width: (parent.width - 12) / 2
                            height: parent.height
                            record: root.selectedRecord
                            side: "right"
                            accentColor: Theme.sage
                        }
                    }
                }

                Item {
                    anchors.fill: parent
                    anchors.margins: 16
                    visible: !root.selectedRecord
                    Column {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 32, 560)
                        spacing: 9
                        Item {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 82; height: 42
                            Rectangle { x: 28; y: 20; width: 26; height: 1; color: Theme.hairlineStrong }
                            Rectangle {
                                x: 0; y: 3; width: 30; height: 34; radius: 7
                                color: Theme.accentMuted; border.color: Theme.accent
                                Text { anchors.centerIn: parent; text: "A"; color: Theme.accent; font.family: Theme.monoFont; font.pixelSize: 14 }
                            }
                            Rectangle {
                                x: 52; y: 3; width: 30; height: 34; radius: 7
                                color: Theme.sageMuted; border.color: Theme.sage
                                Text { anchors.centerIn: parent; text: "B"; color: Theme.sage; font.family: Theme.monoFont; font.pixelSize: 14 }
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Analysis.busy
                                ? L10n.t("center.analyzingTitle")
                                : root.sourceFiles.length === 0
                                    ? L10n.t("center.emptyTitle")
                                    : root.analysisCompleted
                                        ? (Analysis.matchCount > 0 ? L10n.t("center.completedTitle") : L10n.t("center.noMatchesTitle"))
                                        : L10n.t("center.readyTitle")
                            color: Theme.textPrimary
                            font.family: Theme.displayFont
                            font.pixelSize: 20
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }
                        Text { font.family: Theme.fontFamily;
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Analysis.busy
                                ? L10n.t("center.analyzingHint")
                                : root.sourceFiles.length === 0
                                    ? L10n.t("center.emptyHint")
                                    : root.analysisCompleted
                                        ? (Analysis.matchCount > 0 ? L10n.t("center.completedHint") : L10n.t("center.noMatchesHint"))
                                        : L10n.t("center.readyHint")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }
                        PfButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.sourceFiles.length === 0 && !Analysis.busy
                            text: L10n.t("sources.add")
                            onClicked: root.addRequested()
                        }
                        PfButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.sourceFiles.length > 0 && !Analysis.busy
                            text: root.analysisCompleted ? L10n.t("search.restart") : L10n.t("search.start")
                            onClicked: root.analyzeRequested()
                        }
                        Text { font.family: Theme.fontFamily;
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: Analysis.busy
                            text: L10n.status(Analysis.progressStage) + " · " + Math.round(Analysis.progress * 100) + "%"
                            color: Theme.accent
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
