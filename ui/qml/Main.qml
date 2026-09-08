// Main window (spec section 6): top bar with GPU badge + stat chips,
// left panel (sources/sliders), center preview, right results.
// Stage 0: layout skeleton and theme wiring only — no logic yet.
import QtQuick
import QtQuick.Controls.Basic
import PfUi
import PfUiBridge

ApplicationWindow {
    id: root

    width: 1280
    height: 800
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: L10n.t("app.title")
    color: Theme.background

    // Splash → Main transition (stage 0 demo flow)
    Loader {
        id: splashLoader
        anchors.fill: parent
        active: true
        sourceComponent: Splash {}
    }
    Timer {
        interval: 1500
        running: splashLoader.active
        onTriggered: splashLoader.active = false
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ── Top bar ────────────────────────────────────────────────
        Rectangle {
            width: parent.width
            height: Theme.topBarHeight
            color: Theme.panel

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.margins
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: L10n.t("app.title")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                }

                // GPU badge (stage 0: value from AppInfo bridge)
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: gpuBadgeText.implicitWidth + 16
                    height: 22
                    radius: Theme.radiusButton
                    color: Theme.sageMuted

                    Text {
                        id: gpuBadgeText
                        anchors.centerIn: parent
                        text: L10n.t("top.gpu").arg(AppInfo.gpuBackend)
                        color: Theme.sage
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        // ── Three-pane workspace ───────────────────────────────────
        Row {
            width: parent.width
            height: parent.height - Theme.topBarHeight
            spacing: 0

            // Left: sources + sliders (placeholders, stage 5)
            Rectangle {
                width: Theme.sidePanelWidth
                height: parent.height
                color: Theme.panel

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.margins
                    spacing: 8

                    Text {
                        text: L10n.t("panel.sources")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: L10n.t("panel.sliders")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBody
                    }
                }
            }

            // Center: preview / comparison / timeline
            Rectangle {
                width: parent.width - 2 * Theme.sidePanelWidth
                height: parent.height
                color: Theme.background

                Column {
                    anchors.centerIn: parent
                    spacing: 8

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: L10n.t("center.preview")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTitle
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: L10n.t("center.empty")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBody
                    }
                }
            }

            // Right: results (placeholders, stage 5)
            Rectangle {
                width: Theme.sidePanelWidth
                height: parent.height
                color: Theme.panel

                Text {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: Theme.margins
                    text: L10n.t("panel.results")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
