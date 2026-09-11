// Main window (spec section 6): top bar with GPU badge + stat chips,
// left panel (sources/sliders), center preview, right results.
// Stage 0: layout skeleton and theme wiring only — no logic yet.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
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

    property var sourceFiles: []

    FileDialog {
        id: fileDialog
        title: "Выберите видеофайлы"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Видео (*.mp4 *.mov *.mkv *.avi *.webm)", "Все файлы (*)"]
        onAccepted: {
            for (const url of selectedFiles) {
                const path = decodeURIComponent(url.toString().replace(/^file:\/\//, ""))
                if (root.sourceFiles.indexOf(path) < 0) root.sourceFiles.push(path)
            }
            root.sourceFilesChanged()
            Analysis.inspectFiles(root.sourceFiles)
        }
    }

    // Splash → Main transition
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

                // GPU badge — live value from the pfgpu probe (stage 1).
                // Sage marks "accelerated", plain panel marks CPU: the colour
                // carries the meaning, the text carries the detail.
                Rectangle {
                    id: gpuBadge
                    anchors.verticalCenter: parent.verticalCenter
                    width: gpuBadgeText.implicitWidth + 16
                    height: 22
                    radius: Theme.radiusButton
                    color: AppInfo.backendIsGpu ? Theme.sageMuted : Theme.panelAlt

                    Text {
                        id: gpuBadgeText
                        anchors.centerIn: parent
                        text: L10n.t("top.gpu").arg(AppInfo.gpuSummary)
                        color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    MouseArea {
                        id: gpuBadgeHover
                        anchors.fill: parent
                        hoverEnabled: true
                    }

                    ToolTip.visible: gpuBadgeHover.containsMouse
                    ToolTip.delay: 300
                    ToolTip.text: L10n.t("top.gpu.tooltip")
                        .arg(AppInfo.gpuBackend)
                        .arg(AppInfo.ortVersion)
                }
            }
        }

        // ── Three-pane workspace ───────────────────────────────────
        Row {
            width: parent.width
            height: parent.height - Theme.topBarHeight
            spacing: 0

            // Left: sources and analysis controls
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
                    Rectangle { width: parent.width; height: 1; color: Theme.border }
                    Button { text: "Добавить видео"; width: parent.width; onClicked: fileDialog.open() }
                    Button { text: "Очистить список"; width: parent.width; enabled: root.sourceFiles.length > 0; onClicked: { root.sourceFiles = []; } }
                    ListView {
                        width: parent.width; height: 130; clip: true; model: root.sourceFiles
                        delegate: Text { width: ListView.view.width; text: (index + 1) + ". " + modelData.split("/").pop(); elide: Text.ElideMiddle; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                    }
                    Text {
                        text: L10n.t("panel.sliders")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBody
                    }
                    Slider { id: similarity; width: parent.width; from: 0.5; to: 0.99; value: 0.85; ToolTip.visible: hovered; ToolTip.text: "Порог схожести: " + value.toFixed(2) }
                    Slider { id: candidate; width: parent.width; from: 0.1; to: 0.95; value: 0.55; ToolTip.visible: hovered; ToolTip.text: "Порог кандидата: " + value.toFixed(2) }
                    Text { text: "Порог схожести  " + Math.round(similarity.value * 100) + "%"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                    Text { text: "Порог кандидата  " + Math.round(candidate.value * 100) + "%"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                    Button { text: Analysis.busy ? "Анализ выполняется…" : "Запустить анализ сцен"; width: parent.width; enabled: root.sourceFiles.length > 0 && !Analysis.busy; onClicked: Analysis.analyzeFiles(root.sourceFiles) }
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
                    Text { text: Analysis.status.length > 0 ? Analysis.status : "Добавьте видео слева, чтобы начать"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeBody; wrapMode: Text.WordWrap; width: parent.width }
                    Text { text: Analysis.fileCount > 0 ? Analysis.fileCount + " файлов · " + Math.round(Analysis.durationSeconds) + " с · " + Analysis.frameCount + " кадров · " + Analysis.sceneCount + " сцен · " + Analysis.poseDetectionCount + " поз" : ""; color: Theme.sage; font.pixelSize: Theme.fontSizeSmall }
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
