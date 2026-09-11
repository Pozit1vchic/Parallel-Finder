import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
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
            height: 72
            color: Theme.canvas
            Row {
                anchors.fill: parent
                anchors.leftMargin: 40
                anchors.rightMargin: 40
                spacing: 14
                Text { anchors.verticalCenter: parent.verticalCenter; text: "✳"; color: Theme.accent; font.pixelSize: 22 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "Parallel Finder"; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 21 }
                Item { width: Math.max(10, parent.width - 470); height: 1 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "движение / повторение"; color: Theme.textDisabled; font.pixelSize: Theme.fontSizeSmall }
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: gpuText.implicitWidth + 22; height: 28; radius: 14; color: AppInfo.backendIsGpu ? Theme.sageMuted : "#242421"
                    Text { id: gpuText; anchors.centerIn: parent; text: AppInfo.backendIsGpu ? "●  " + AppInfo.gpuSummary : "○  CPU"; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.pixelSize: 11 }
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        Row {
            width: parent.width
            height: parent.height - 73
            anchors.leftMargin: 40
            anchors.rightMargin: 40
            spacing: 60

            Item {
                width: parent.width * 0.39
                height: parent.height
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 30
                    spacing: 22
                    Text { text: Analysis.fileCount > 0 ? "Ваши параллели" : "Найдите движение,\nкоторое повторяется"; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 46; lineHeight: 0.94; wrapMode: Text.WordWrap }
                    Text { text: Analysis.fileCount > 0 ? "Сцены разобраны. Пары движения готовы к просмотру." : "Parallel Finder видит не кадры, а ритм между ними."; color: Theme.textSecondary; font.pixelSize: 16; lineHeight: 1.25; wrapMode: Text.WordWrap; width: 380 }
                    Row {
                        spacing: 12
                        Button { width: 170; height: 44; text: Analysis.busy ? "Идёт анализ…" : "Добавить видео"; onClicked: fileDialog.open()
                            contentItem: Text { text: parent.text; color: Theme.canvas; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 13; font.weight: Font.DemiBold }
                            background: Rectangle { radius: 22; color: Theme.textPrimary }
                        }
                        Button { width: 128; height: 44; text: "Запустить"; enabled: root.sourceFiles.length > 0 && !Analysis.busy; onClicked: Analysis.analyzeFiles(root.sourceFiles)
                            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.accent : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 13 }
                            background: Rectangle { radius: 22; color: "transparent"; border.color: parent.enabled ? Theme.accent : Theme.textDisabled }
                        }
                    }
                    Rectangle { width: 390; height: 1; color: Theme.hairline }
                    Row { spacing: 28
                        Column {
                            Text { text: Analysis.fileCount; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 28 }
                            Text { text: "файлов"; color: Theme.textDisabled; font.pixelSize: 11 }
                        }
                        Column {
                            Text { text: Analysis.poseDetectionCount; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 28 }
                            Text { text: "поз найдено"; color: Theme.textDisabled; font.pixelSize: 11 }
                        }
                        Column {
                            Text { text: Analysis.matchCount; color: Theme.accent; font.family: "Georgia"; font.pixelSize: 28 }
                            Text { text: "параллелей"; color: Theme.textDisabled; font.pixelSize: 11 }
                        }
                    }
                }
            }

            Item {
                width: parent.width * 0.54
                height: parent.height
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: parent.height * 0.72
                    color: Theme.heroPanel
                    radius: 14
                    border.color: Theme.hairline
                    clip: true
                    Rectangle { x: 0; y: 0; width: parent.width * 0.55; height: parent.height; color: Theme.sageMuted; opacity: 0.22 }
                    Rectangle { x: parent.width * 0.58; y: parent.height * 0.10; width: parent.width * 0.33; height: parent.height * 0.80; color: Theme.accentMuted; opacity: 0.24; radius: 180 }
                    Column { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 24; spacing: 5
                        Text { text: "MOTION FIELD"; color: Theme.textDisabled; font.pixelSize: 10; font.weight: Font.DemiBold }
                        Text { text: Analysis.status.length > 0 ? Analysis.status : "Ожидание первого движения"; color: Theme.textPrimary; font.pixelSize: 14 }
                    }
                    Column { anchors.centerIn: parent; spacing: 12
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 250; height: 250; radius: 125; color: "transparent"; border.color: Theme.accent; border.width: 1; opacity: 0.76
                            Rectangle { anchors.centerIn: parent; width: 170; height: 170; radius: 85; color: "transparent"; border.color: Theme.sage; border.width: 1; opacity: 0.85
                                Rectangle { anchors.centerIn: parent; width: 74; height: 74; radius: 37; color: Theme.panel; border.color: Theme.textPrimary; border.width: 1 }
                            }
                        }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: Analysis.fileCount > 0 ? "Карта собрана" : "Здесь появится карта движения"; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 18 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: Analysis.fileCount > 0 ? Analysis.sceneCount + " сцен  ·  " + Analysis.matchCount + " пар" : "Добавьте видео, чтобы начать"; color: Theme.textSecondary; font.pixelSize: 12 }
                    }
                    Text { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 24; text: Analysis.durationSeconds > 0 ? Math.round(Analysis.durationSeconds) + " секунд материала" : "Нет выбранного материала"; color: Theme.textDisabled; font.pixelSize: 11 }
                }
                Rectangle { anchors.bottom: parent.bottom; anchors.bottomMargin: 42; width: parent.width; height: 42; color: Theme.heroPanel; radius: 6; border.color: Theme.hairline
                    Rectangle { x: 12; y: 9; width: parent.width * 0.20; height: 24; radius: 4; color: Theme.sageMuted }
                    Rectangle { x: parent.width * 0.48; y: 9; width: 2; height: 24; color: Theme.accent }
                    Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; text: "timeline"; color: Theme.textDisabled; font.pixelSize: 10 }
                }
            }
        }
    }
}
