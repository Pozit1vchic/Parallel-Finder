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
    property int selectedResultIndex: -1

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

    Dialog {
        id: settingsDialog
        modal: true
        title: "Настройки Parallel Finder"
        width: 460
        standardButtons: Dialog.Close
        contentItem: Rectangle {
            implicitHeight: 330
            color: Theme.heroPanel
            Column { anchors.fill: parent; anchors.margins: 24; spacing: 16
                Text { text: "Настройки анализа"; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 22 }
                Text { text: "Параметры сохраняются для следующих запусков."; color: Theme.textSecondary; font.pixelSize: 12 }
                Rectangle { width: parent.width; height: 1; color: Theme.border }
                Row { width: parent.width
                    Text { text: "Провайдер"; color: Theme.textPrimary; font.pixelSize: 12 }
                    Item { width: parent.width - 180; height: 1 }
                    Text { text: AppInfo.gpuBackend; color: Theme.sage; font.pixelSize: 12 }
                }
                Row { width: parent.width
                    Text { text: "Тема"; color: Theme.textPrimary; font.pixelSize: 12 }
                    Item { width: parent.width - 180; height: 1 }
                    Text { text: "Parallel / dark"; color: Theme.textSecondary; font.pixelSize: 12 }
                }
                Row { width: parent.width
                    Text { text: "Кэш"; color: Theme.textPrimary; font.pixelSize: 12 }
                    Item { width: parent.width - 180; height: 1 }
                    Text { text: "8 ГБ · LocalAppData"; color: Theme.textSecondary; font.pixelSize: 12 }
                }
                Rectangle { width: parent.width; height: 42; radius: 8; color: Theme.panelAlt; border.color: Theme.border
                    Text { anchors.centerIn: parent; text: "Сбросить параметры анализа"; color: Theme.textSecondary; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; onClicked: { similarity.value = 0.85; candidate.value = 0.55 } }
                }
            }
        }
    }

    Dialog {
        id: exportDialog
        modal: true
        title: "Экспорт результатов"
        width: 440
        standardButtons: Dialog.Cancel
        contentItem: Rectangle {
            implicitHeight: 260
            color: Theme.heroPanel
            Column { anchors.fill: parent; anchors.margins: 24; spacing: 14
                Text { text: "Подготовить результаты"; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 22 }
                Text { text: Analysis.matchCount + " выбранных параллелей"; color: Theme.textSecondary; font.pixelSize: 12 }
                ComboBox { id: exportFormat; width: parent.width; model: ["JSON", "CSV", "TXT", "EDL", "FCPXML"] }
                Button { width: parent.width; height: 40; text: "Сохранить " + exportFormat.currentText; onClicked: exportDialog.close()
                    contentItem: Text { text: parent.text; color: Theme.canvas; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.weight: Font.DemiBold }
                    background: Rectangle { radius: 8; color: Theme.textPrimary }
                }
            }
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
                Text { text: "Parallel Finder"; color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 20 }
            }
            Row { anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                Button { text: "Настройки"; onClicked: settingsDialog.open()
                    contentItem: Text { text: parent.text; color: Theme.textSecondary; font.pixelSize: 11 }
                    background: Rectangle { color: "transparent" }
                }
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
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            clip: true

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
                    Row { width: parent.width; spacing: 6
                        Button { width: (parent.width - 6) / 2; height: 30; text: "Выбрать папку"
                            contentItem: Text { text: parent.text; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                            background: Rectangle { radius: 6; color: Theme.panelAlt }
                        }
                        Button { width: (parent.width - 6) / 2; height: 30; text: "Очистить"; enabled: root.sourceFiles.length > 0; onClicked: { root.sourceFiles = []; root.sourceFilesChanged() }
                            contentItem: Text { text: parent.text; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                            background: Rectangle { radius: 6; color: Theme.panelAlt }
                        }
                    }
                    Button { width: parent.width; height: 28; text: "Удалить выбранное"; enabled: root.sourceFiles.length > 0
                        contentItem: Text { text: parent.text; color: Theme.textDisabled; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                        background: Rectangle { color: "transparent" }
                    }
                    CheckBox { text: "Найти человека по фото"; checked: false; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: 24; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 } }
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
                    Text { text: "Качество анализа"; color: Theme.textSecondary; font.pixelSize: 11 }
                    Row { width: parent.width; spacing: 4
                        property int selectedQuality: 2
                        Repeater { model: ["Быстро", "Средне", "Максимум"]
                            delegate: Button { width: (parent.width - 8) / 3; height: 28; text: modelData; onClicked: parent.parent.selectedQuality = index
                                contentItem: Text { text: parent.text; color: parent.parent.selectedQuality === index ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 9 }
                                background: Rectangle { radius: 5; color: parent.parent.selectedQuality === index ? Theme.accent : Theme.panelAlt }
                            }
                        }
                    }
                    Text { text: "Опции"; color: Theme.textSecondary; font.pixelSize: 11 }
                    CheckBox { text: "Нормализация размера"; checked: true; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: 24; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 } }
                    CheckBox { text: "Зеркальные позы"; checked: true; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: 24; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 } }
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
                        model: [["КАДРЫ", Analysis.frameCount], ["ПОВТОРЫ", Analysis.matchCount], ["ДЛИТЕЛЬНОСТЬ", Math.round(Analysis.durationSeconds) + " с"], ["СХОЖЕСТЬ", Analysis.matchCount > 0 ? "85%" : "—"], ["ОСТАЛОСЬ", Analysis.busy ? "…" : "00:00"], ["ПРОГРЕСС", Analysis.busy ? "…" : (Analysis.fileCount > 0 ? "100%" : "0%")]]
                        delegate: Rectangle { width: (parent.width - 50) / 6; height: 68; color: Theme.panel; radius: 9
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
                        Rectangle { width: parent.width; height: 46; color: Theme.panelAlt; radius: 7
                            Column { anchors.fill: parent; anchors.margins: 9; spacing: 4
                                Row { width: parent.width
                                    Text { text: Analysis.busy ? "Обрабатываем материал" : "Прогресс"; color: Theme.textSecondary; font.pixelSize: 10 }
                                    Item { width: parent.width - 110; height: 1 }
                                    Text { text: Analysis.busy ? "…" : (Analysis.fileCount > 0 ? "готово" : "ожидание"); color: Theme.sage; font.pixelSize: 10 }
                                }
                                Rectangle { width: parent.width; height: 4; radius: 2; color: Theme.canvas; Rectangle { width: Analysis.fileCount > 0 ? parent.width : 0; height: parent.height; radius: 2; color: Theme.accent } }
                            }
                        }
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
                            Column { anchors.centerIn: parent; spacing: 12; visible: root.selectedResultIndex < 0
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 190; height: 190; radius: 95; color: "transparent"; border.color: Theme.accent; border.width: 1
                                    Rectangle { anchors.centerIn: parent; width: 128; height: 128; radius: 64; color: "transparent"; border.color: Theme.sage; border.width: 1
                                        Rectangle { anchors.centerIn: parent; width: 48; height: 48; radius: 24; color: Theme.panel; border.color: Theme.textPrimary; border.width: 1 }
                                    }
                                }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.selectedResultIndex >= 0 ? "Пара " + (root.selectedResultIndex + 1) + " выбрана" : (Analysis.fileCount > 0 ? "Движение готово к сравнению" : "Добавьте видео, чтобы начать"); color: Theme.textPrimary; font.family: "Georgia"; font.pixelSize: 17 }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.selectedResultIndex >= 0 ? Analysis.resultItems[root.selectedResultIndex] : (Analysis.fileCount > 0 ? Analysis.poseDetectionCount + " поз  ·  " + Analysis.matchCount + " пар" : "В центре появится превью пары"); color: Theme.textSecondary; font.pixelSize: 11 }
                            }
                            Row { anchors.centerIn: parent; width: parent.width - 36; height: parent.height - 36; spacing: 10; visible: root.selectedResultIndex >= 0
                                Rectangle { width: (parent.width - 10) / 2; height: parent.height; color: Theme.panel; radius: 8; border.color: Theme.hairline
                                    Column { anchors.fill: parent; anchors.margins: 12; spacing: 8
                                        Text { text: "A  ·  " + (Analysis.resultItems[root.selectedResultIndex] || ""); color: Theme.textPrimary; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
                                        Rectangle { width: parent.width; height: parent.height - 42; color: Theme.canvas; radius: 5
                                            Image { anchors.fill: parent; anchors.margins: 2; source: root.selectedResultIndex >= 0 && Analysis.previewA.length > root.selectedResultIndex ? Analysis.previewA[root.selectedResultIndex] : ""; fillMode: Image.PreserveAspectCrop; smooth: true; visible: source.length > 0 }
                                            Column { anchors.centerIn: parent; spacing: 6; visible: !(root.selectedResultIndex >= 0 && Analysis.previewA.length > root.selectedResultIndex)
                                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "A"; color: Theme.accent; font.family: "Georgia"; font.pixelSize: 30 }
                                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "левый фрагмент"; color: Theme.textDisabled; font.pixelSize: 10 }
                                            }
                                        }
                                    }
                                }
                                Rectangle { width: (parent.width - 10) / 2; height: parent.height; color: Theme.panel; radius: 8; border.color: Theme.hairline
                                    Column { anchors.fill: parent; anchors.margins: 12; spacing: 8
                                        Text { text: "B  ·  " + (Analysis.resultItems[root.selectedResultIndex] || ""); color: Theme.textPrimary; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width }
                                        Rectangle { width: parent.width; height: parent.height - 42; color: Theme.canvas; radius: 5
                                            Image { anchors.fill: parent; anchors.margins: 2; source: root.selectedResultIndex >= 0 && Analysis.previewB.length > root.selectedResultIndex ? Analysis.previewB[root.selectedResultIndex] : ""; fillMode: Image.PreserveAspectCrop; smooth: true; visible: source.length > 0 }
                                            Column { anchors.centerIn: parent; spacing: 6; visible: !(root.selectedResultIndex >= 0 && Analysis.previewB.length > root.selectedResultIndex)
                                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "B"; color: Theme.sage; font.family: "Georgia"; font.pixelSize: 30 }
                                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "правый фрагмент"; color: Theme.textDisabled; font.pixelSize: 10 }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Row { width: parent.width
                            Text { text: "ТАЙМЛАЙН"; color: Theme.textDisabled; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Item { width: parent.width - 90; height: 1 }
                            Text { text: Analysis.durationSeconds > 0 ? Math.round(Analysis.durationSeconds) + " с" : "—"; color: Theme.textSecondary; font.pixelSize: 10 }
                        }
                        Rectangle { width: parent.width; height: 30; color: Theme.panelAlt; radius: 5
                            Rectangle { x: 8; y: 7; width: parent.width * 0.2; height: 16; color: Theme.sageMuted; radius: 3 }
                            Rectangle { x: root.selectedResultIndex >= 0 ? parent.width * 0.32 : parent.width * 0.52; y: 5; width: 2; height: 20; color: Theme.accent }
                            Rectangle { visible: root.selectedResultIndex >= 0; x: parent.width * 0.68; y: 5; width: 2; height: 20; color: Theme.sage }
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
                    Row { width: parent.width; spacing: 6
                        Button { width: 82; height: 28; text: "‹ Пред."
                            contentItem: Text { text: parent.text; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                            background: Rectangle { radius: 6; color: Theme.panelAlt }
                        }
                        Item { width: parent.width - 176; height: 1 }
                        Button { width: 82; height: 28; text: "След. ›"
                            contentItem: Text { text: parent.text; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                            background: Rectangle { radius: 6; color: Theme.panelAlt }
                        }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.border }
                    Text { text: Analysis.matchCount > 0 ? "Найденные параллели" : "Результаты появятся здесь"; color: Theme.textSecondary; font.pixelSize: 12 }
                    Button { width: parent.width; height: 36; text: "Экспорт результатов"; enabled: Analysis.matchCount > 0; onClicked: exportDialog.open()
                        contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
                        background: Rectangle { radius: 8; color: parent.enabled ? Theme.accent : Theme.panelAlt }
                    }
                    Text { text: "Категории движений"; color: Theme.textSecondary; font.pixelSize: 11 }
                    Row { width: parent.width; spacing: 4
                        Rectangle { width: 42; height: 24; radius: 12; color: Theme.accentMuted; Text { anchors.centerIn: parent; text: "Все"; color: Theme.textPrimary; font.pixelSize: 10 } }
                        Rectangle { width: 72; height: 24; radius: 12; color: Theme.panelAlt; Text { anchors.centerIn: parent; text: "К камере"; color: Theme.textSecondary; font.pixelSize: 10 } }
                        Rectangle { width: 70; height: 24; radius: 12; color: Theme.panelAlt; Text { anchors.centerIn: parent; text: "В сторону"; color: Theme.textSecondary; font.pixelSize: 10 } }
                    }
                    ListView { width: parent.width; height: parent.height - 100; clip: true; model: Analysis.resultItems
                        delegate: Rectangle { width: ListView.view.width; height: 54; color: index === root.selectedResultIndex ? Theme.accentMuted : Theme.panelAlt; radius: 7; anchors.margins: 2
                            Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: Theme.textPrimary; font.pixelSize: 11 }
                            MouseArea { anchors.fill: parent; onClicked: root.selectedResultIndex = index }
                        }
                    }
                }
            }
        }
    }
}
