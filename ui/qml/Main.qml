import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import PfUi
import PfUiBridge

ApplicationWindow {
    id: root
    width: 1360; height: 860; minimumWidth: 1100; minimumHeight: 700
    visible: true; title: L10n.t("app.title"); color: Theme.background
    property var sourceFiles: []

    FileDialog {
        id: fileDialog; title: "Выберите видеофайлы"; fileMode: FileDialog.OpenFiles
        nameFilters: ["Видео (*.mp4 *.mov *.mkv *.avi *.webm)", "Все файлы (*)"]
        onAccepted: {
            for (const url of selectedFiles) {
                const path = decodeURIComponent(url.toString().replace(/^file:\/\//, ""))
                if (root.sourceFiles.indexOf(path) < 0) root.sourceFiles.push(path)
            }
            root.sourceFilesChanged(); Analysis.inspectFiles(root.sourceFiles)
        }
    }
    Loader { id: splashLoader; anchors.fill: parent; active: true; sourceComponent: Splash {} }
    Timer { interval: 1500; running: splashLoader.active; onTriggered: splashLoader.active = false }

    Column {
        anchors.fill: parent; spacing: 0
        Rectangle {
            width: parent.width; height: 64; color: Theme.panel
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
            Row {
                anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 16
                Text { anchors.verticalCenter: parent.verticalCenter; text: "PARALLEL"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "FINDER"; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 17; font.weight: Font.DemiBold }
                Rectangle { width: 1; height: 24; anchors.verticalCenter: parent.verticalCenter; color: Theme.border }
                Text { anchors.verticalCenter: parent.verticalCenter; text: Analysis.busy ? "Собираем карту движения" : "Рабочее пространство"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                Item { width: Math.max(20, parent.width - 560); height: 1 }
                Rectangle { anchors.verticalCenter: parent.verticalCenter; height: 30; width: gpuText.implicitWidth + 26; radius: 15; color: AppInfo.backendIsGpu ? Theme.sageMuted : Theme.panelAlt
                    Text { id: gpuText; anchors.centerIn: parent; text: AppInfo.backendIsGpu ? "●  " + AppInfo.gpuSummary : "○  CPU"; color: AppInfo.backendIsGpu ? Theme.sage : Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "RU"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
            }
        }
        Row { width: parent.width; height: parent.height - 64; spacing: 12; padding: 14
            Rectangle { width: 300; height: parent.height - 28; radius: Theme.radiusCard; color: Theme.panel
                Column { anchors.fill: parent; anchors.margins: 18; spacing: 13
                    Text { text: "Источники"; color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { text: root.sourceFiles.length > 0 ? root.sourceFiles.length + " видео добавлено" : "Начните с одного видео"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                    Rectangle { width: parent.width; height: 118; radius: 10; color: Theme.background; border.color: Theme.border
                        Column { anchors.centerIn: parent; spacing: 7
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sourceFiles.length > 0 ? "●" : "+"; color: Theme.accent; font.pixelSize: 26 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.sourceFiles.length > 0 ? "Файлы готовы" : "Перетащите видео сюда"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                        }
                        MouseArea { anchors.fill: parent; onClicked: fileDialog.open() }
                    }
                    Row { width: parent.width; spacing: 8
                        Button { width: parent.width - 92; text: "Добавить видео"; onClicked: fileDialog.open()
                            contentItem: Text { text: parent.text; color: Theme.background; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold }
                            background: Rectangle { radius: 8; color: parent.down ? Theme.accent : Theme.textPrimary }
                        }
                        Button { width: 84; text: "Очистить"; enabled: root.sourceFiles.length > 0; onClicked: { root.sourceFiles = []; root.sourceFilesChanged() }
                            contentItem: Text { text: parent.text; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
                            background: Rectangle { radius: 8; color: Theme.panelAlt }
                        }
                    }
                    ListView { width: parent.width; height: 86; clip: true; model: root.sourceFiles; delegate: Text { width: ListView.view.width; height: 24; text: (index + 1) + "  " + modelData.split("/").pop(); elide: Text.ElideMiddle; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall; verticalAlignment: Text.AlignVCenter } }
                    Rectangle { width: parent.width; height: 1; color: Theme.border }
                    Text { text: "Параметры поиска"; color: Theme.textPrimary; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Text { text: "Порог схожести  " + Math.round(similarity.value * 100) + "%"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                    Slider { id: similarity; width: parent.width; from: 0.5; to: 0.99; value: Analysis.similarityThreshold; onValueChanged: Analysis.similarityThreshold = value }
                    Text { text: "Порог кандидата  " + Math.round(candidate.value * 100) + "%"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                    Slider { id: candidate; width: parent.width; from: 0.1; to: 0.95; value: Analysis.candidateThreshold; onValueChanged: Analysis.candidateThreshold = value }
                    Item { width: 1; height: 1 }
                    Button { width: parent.width; height: 42; text: Analysis.busy ? "Анализ выполняется…" : "Запустить анализ"; enabled: root.sourceFiles.length > 0 && !Analysis.busy; onClicked: Analysis.analyzeFiles(root.sourceFiles)
                        contentItem: Text { text: parent.text; color: Theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 13; font.weight: Font.DemiBold }
                        background: Rectangle { radius: 9; color: parent.enabled ? Theme.accent : Theme.panelAlt }
                    }
                }
            }
            Column { width: parent.width - 632; height: parent.height - 28; spacing: 12
                Row { width: parent.width; height: 76; spacing: 10
                    Repeater { model: [["ФАЙЛЫ", Analysis.fileCount], ["КАДРЫ", Analysis.frameCount], ["СЦЕНЫ", Analysis.sceneCount], ["ПАРЫ", Analysis.matchCount]]
                        delegate: Rectangle { width: (parent.width - 30) / 4; height: 76; radius: 10; color: Theme.panel
                            Column { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                                Text { text: modelData[0]; color: Theme.textDisabled; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Text { text: modelData[1]; color: Theme.textPrimary; font.pixelSize: 21; font.weight: Font.DemiBold }
                            }
                        }
                    }
                }
                Rectangle { width: parent.width; height: parent.height - 88; radius: Theme.radiusCard; color: Theme.panel
                    Column { anchors.fill: parent; anchors.margins: 20; spacing: 12
                        Row { width: parent.width
                            Text { text: "Карта движения"; color: Theme.textPrimary; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Item { width: parent.width - 260; height: 1 }
                            Text { text: Analysis.status.length > 0 ? Analysis.status : "Ожидание анализа"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                        }
                        Rectangle { width: parent.width; height: parent.height - 100; radius: 10; color: Theme.background; border.color: Theme.border
                            Column { anchors.centerIn: parent; spacing: 12
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 96; height: 96; radius: 48; color: Theme.accentMuted; border.color: Theme.accent; border.width: 1
                                    Rectangle { anchors.centerIn: parent; width: 54; height: 54; radius: 27; color: Theme.panel; border.color: Theme.sage; border.width: 1 }
                                }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Analysis.fileCount > 0 ? "Сцены и движения готовы к сравнению" : "Добавьте видео, чтобы увидеть движение"; color: Theme.textPrimary; font.pixelSize: 15 }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Analysis.fileCount > 0 ? Analysis.poseDetectionCount + " поз распознано" : "Ищем повторяющиеся жесты и траектории"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                            }
                        }
                        Row { width: parent.width
                            Text { text: "ТАЙМЛАЙН"; color: Theme.textDisabled; font.pixelSize: 10; font.weight: Font.DemiBold }
                            Item { width: parent.width - 100; height: 1 }
                            Text { text: Analysis.durationSeconds > 0 ? Math.round(Analysis.durationSeconds) + " с" : "—"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                        }
                        Rectangle { width: parent.width; height: 34; radius: 6; color: Theme.panelAlt; border.color: Theme.border; Rectangle { x: 8; y: 7; width: parent.width * 0.18; height: 20; radius: 4; color: Theme.sageMuted } }
                    }
                }
            }
            Rectangle { width: 300; height: parent.height - 28; radius: Theme.radiusCard; color: Theme.panel
                Column { anchors.fill: parent; anchors.margins: 18; spacing: 12
                    Row { width: parent.width
                        Text { text: "Результаты"; color: Theme.textPrimary; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item { width: parent.width - 170; height: 1 }
                        Text { text: Analysis.matchCount; color: Theme.accent; font.pixelSize: 16; font.weight: Font.DemiBold }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.border }
                    Column { anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 120; spacing: 8
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: Analysis.matchCount > 0 ? "Пары найдены" : "Пока пусто"; color: Theme.textPrimary; font.pixelSize: 15 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: Analysis.matchCount > 0 ? "Выберите результат для просмотра" : "Запустите анализ видео"; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeSmall }
                    }
                }
            }
        }
    }
}
