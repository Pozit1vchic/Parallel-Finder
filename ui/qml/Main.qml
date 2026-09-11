import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import PfUi
import PfUiBridge

ApplicationWindow {
    id: root
    width: 1360
    height: 860
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: L10n.t("app.title")
    color: Theme.canvas
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

    Loader { id: splashLoader; anchors.fill: parent; active: true; sourceComponent: Splash {} }
    Timer { interval: 1500; running: splashLoader.active; onTriggered: splashLoader.active = false }

    Column {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            width: parent.width
            height: 60
            color: Theme.canvas
            Row { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                Text { text: "✳"; color: Theme.accent; font.pixelSize: 20 }
                Text { text: "Parallel Finder"; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 20 }
                Text { text: "·  motion study"; color: Theme.textDisabled; font.pixelSize: 11 }
            }
            Row { anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                Text { text: Analysis.busy ? "Анализируем" : "Готово к работе"; color: Theme.textSecondary; font.pixelSize: 11 }
                Rectangle { width: gpuLabel.implicitWidth + 18; height: 26; radius: 13; color: AppInfo.backendIsGpu ? Theme.sageMuted : Theme.panelAlt
                    Text { id: gpuLabel; anchors.centerIn: parent; text: AppInfo.backendIsGpu ? "●  " + AppInfo.gpuSummary : "○  CPU"; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.pixelSize: 10 }
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height - 61
            spacing: 12
            anchors.leftMargin: 14
            anchors.rightMargin: 14

            Rectangle {
                width: 286
                height: parent.height - 28
                color: Theme.panel
                radius: Theme.radiusCard
                layer.enabled: true
                layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.42); shadowBlur: 0.55; shadowVerticalOffset: 8 }
                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Text { text: "Источники"; color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { text: root.sourceFiles.length > 0 ? root.sourceFiles.length + " видео добавлено" : "Видео для сравнения"; color: Theme.textSecondary; font.pixelSize: 11 }
                    Button {
                        width: parent.width
                        height: 38
                        text: "+  Добавить видео"
                        onClicked: fileDialog.open()
                        contentItem: Text { text: parent.text; color: Theme.canvas; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.weight: Font.DemiBold }
                        background: Rectangle { radius: 8; color: Theme.textPrimary }
                    }
                    ListView {
                        width: parent.width
                        height: 130
                        clip: true
                        model: root.sourceFiles
                        delegate: Rectangle { width: ListView.view.width; height: 30; color: index % 2 ? "transparent" : Theme.panelAlt; radius: 5
                            Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 12; text: (index + 1) + "  " + modelData.split("/").pop(); elide: Text.ElideMiddle; color: Theme.textSecondary; font.pixelSize: 11 }
                        }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.border }
                    Text { text: "Порог схожести"; color: Theme.textSecondary; font.pixelSize: 11 }
                    Row { width: parent.width
                        Text { text: Math.round(similarity.value * 100) + "%"; color: Theme.accent; font.pixelSize: 12 }
                        Item { width: parent.width - 42; height: 1 }
                    }
                    Slider { id: similarity; width: parent.width; from: 0.5; to: 0.99; value: Analysis.similarityThreshold; onValueChanged: Analysis.similarityThreshold = value }
                    Text { text: "Порог кандидата"; color: Theme.textSecondary; font.pixelSize: 11 }
                    Row { width: parent.width
                        Text { text: Math.round(candidate.value * 100) + "%"; color: Theme.accent; font.pixelSize: 12 }
                        Item { width: parent.width - 42; height: 1 }
                    }
                    Slider { id: candidate; width: parent.width; from: 0.1; to: 0.95; value: Analysis.candidateThreshold; onValueChanged: Analysis.candidateThreshold = value }
                    Item { width: 1; height: 1 }
                    Button {
                        width: parent.width
                        height: 40
                        text: Analysis.busy ? "Идёт анализ…" : "Запустить анализ"
                        enabled: root.sourceFiles.length > 0 && !Analysis.busy
                        onClicked: Analysis.analyzeFiles(root.sourceFiles)
                        contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.weight: Font.DemiBold }
                        background: Rectangle { radius: 8; color: parent.enabled ? Theme.accent : Theme.panelAlt }
                    }
                    Text { width: parent.width; text: Analysis.status; color: Theme.textDisabled; font.pixelSize: 10; wrapMode: Text.WordWrap }
                }
            }

            Column {
                width: parent.width - 610
                height: parent.height - 28
                spacing: 12
                Row {
                    width: parent.width
                    height: 68
                    spacing: 10
                    Repeater {
                        model: [["ФАЙЛЫ", Analysis.fileCount], ["КАДРЫ", Analysis.frameCount], ["СЦЕНЫ", Analysis.sceneCount], ["ПАРЫ", Analysis.matchCount]]
                        delegate: Rectangle { width: (parent.width - 30) / 4; height: 68; color: Theme.panel; radius: 9
                            Column { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                Text { text: modelData[0]; color: Theme.textDisabled; font.pixelSize: 9; font.weight: Font.DemiBold }
                                Text { text: modelData[1]; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 21 }
                            }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    height: parent.height - 92
                    color: Theme.panel
                    radius: Theme.radiusCard
                    layer.enabled: true
                    layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.48); shadowBlur: 0.65; shadowVerticalOffset: 10 }
                    Column {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10
                        Row { width: parent.width
                            Text { text: "Сравнение"; color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                            Item { width: parent.width - 300; height: 1 }
                            Text { text: Analysis.fileCount > 0 ? "карта движения" : "ожидание материала"; color: Theme.textDisabled; font.pixelSize: 10 }
                        }
                        Rectangle {
                            width: parent.width
                            height: parent.height - 105
                            color: Theme.canvas
                            radius: 10
                            border.color: Theme.hairline
                            Column { anchors.centerIn: parent; spacing: 12
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 190; height: 190; radius: 95; color: "transparent"; border.color: Theme.accent; border.width: 1
                                    Rectangle { anchors.centerIn: parent; width: 128; height: 128; radius: 64; color: "transparent"; border.color: Theme.sage; border.width: 1
                                        Rectangle { anchors.centerIn: parent; width: 48; height: 48; radius: 24; color: Theme.panel; border.color: Theme.textPrimary; border.width: 1 }
                                    }
                                }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Analysis.fileCount > 0 ? "Движение готово к сравнению" : "Добавьте видео, чтобы начать"; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 17 }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Analysis.fileCount > 0 ? Analysis.poseDetectionCount + " поз  ·  " + Analysis.matchCount + " пар" : "В центре появится превью пары"; color: Theme.textSecondary; font.pixelSize: 11 }
                            }
                        }
                        Row { width: parent.width
                            Text { text: "ТАЙМЛАЙН"; color: Theme.textDisabled; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Item { width: parent.width - 90; height: 1 }
                            Text { text: Analysis.durationSeconds > 0 ? Math.round(Analysis.durationSeconds) + " с" : "—"; color: Theme.textSecondary; font.pixelSize: 10 }
                        }
                        Rectangle { width: parent.width; height: 30; color: Theme.panelAlt; radius: 5
                            Rectangle { x: 8; y: 7; width: parent.width * 0.2; height: 16; color: Theme.sageMuted; radius: 3 }
                            Rectangle { x: parent.width * 0.52; y: 5; width: 2; height: 20; color: Theme.accent }
                        }
                    }
                }
            }

            Rectangle {
                width: 286
                height: parent.height - 28
                color: Theme.panel
                radius: Theme.radiusCard
                layer.enabled: true
                layer.effect: MultiEffect { shadowEnabled: true; shadowColor: Qt.rgba(0, 0, 0, 0.42); shadowBlur: 0.55; shadowVerticalOffset: 8 }
                Column { anchors.fill: parent; anchors.margins: 16; spacing: 12
                    Row { width: parent.width
                        Text { text: "Результаты"; color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item { width: parent.width - 170; height: 1 }
                        Text { text: Analysis.matchCount; color: Theme.accent; font.family: "Georgia"; font.pixelSize: 20 }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.border }
                    Text { text: Analysis.matchCount > 0 ? "Найденные параллели" : "Результаты появятся здесь"; color: Theme.textSecondary; font.pixelSize: 12 }
                    ListView { width: parent.width; height: parent.height - 100; clip: true; model: Analysis.resultItems
                        delegate: Rectangle { width: ListView.view.width; height: 54; color: Theme.panelAlt; radius: 7; anchors.margins: 2
                            Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: Theme.textPrimary; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
    }
}
