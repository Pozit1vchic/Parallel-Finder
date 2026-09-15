# Полный аудит Parallel Finder

Дата ревизии: **15 сентября 2026**

Ветка: `main`

Последняя проверенная ревизия включает изменения после `e30b0fd`: matcher,
кэш анализа, provider-download fallback и batch-путь для GPU-моделей были
перепроверены локальной сборкой.

Этот документ сверяет текущий код с замечаниями из переписки и разделяет
реализацию, частичную готовность и то, что пока нельзя честно назвать
проверенным.

## Легенда

| Статус | Значение |
| --- | --- |
| **Готово** | Есть реализация в коде и она прошла доступную локальную проверку. |
| **Частично** | Архитектура подключена, но нужен реальный asset, GPU или визуальная проверка. |
| **Не подтверждено** | Код есть, но сценарий ещё не воспроизводился на настоящих данных. |
| **Осталось** | В текущей версии требования нет или оно требует отдельной стадии. |

## Итог в одном абзаце

Core-пайплайн больше не сводится к сравнению одного кадра: он строит
временные окна, проверяет разные временные сэмплы, motion/static policy,
scene/track ограничения, DTW и NMS. UI содержит рабочие состояния, preview A/B,
editable sliders, выбор режимов, моделей, provider и accent color. Главные
неподтверждённые места находятся за пределами чистой сборки: GitHub Release с
реальными пакетами, body-ReID на размеченном identity-set, все GPU-провайдеры
на целевых драйверах и визуальная проверка на реальном DPI.

## Матрица пользовательских замечаний

| Замечание | Статус | Что реально сделано / что проверить |
| --- | --- | --- |
| Header, DML-бейдж и кнопка настроек на одной оси | **Готово** | Header использует layout-выравнивание; статус ограничен и центрирован. Нужна финальная проверка на Verdana/DPI. |
| Заголовок «Источники» и счётчик | **Готово** | Один `RowLayout`/колонка, текст не должен плавать по вертикали. |
| Dropzone: плюс убрать, текст центрировать | **Готово** | Иконка удалена; заголовок и подсказка центрированы и переносятся без обрезания. |
| Кнопки и карточки вылезают за контейнер | **Частично** | Введены `Layout.fillWidth`, `minimumWidth: 0`, word-wrap и scrollable rail. Живая проверка длинным шрифтом ещё обязательна. |
| Advanced-блок не открывался или терял поля | **Готово** | Убран декоративный плюс, заголовок центрирован, `Flickable.contentHeight` учитывает `childrenRect`; карточки участвуют в implicit height. |
| Диапазон слайдера должен быть виден | **Готово** | `PfSliderField` показывает min/max под шкалой и оставляет числовой `QML`-инпут справа. |
| Числовое значение редактируется с клавиатуры | **Готово** | `PfTextField` с validator, Enter и `editingFinished`. |
| Во время анализа показывать только «анализируем» | **Готово** | `MotionCenter` выбирает analyzing-state раньше empty-state; кнопка добавления скрыта во время анализа. |
| Чёрные A/B preview | **Готово на уровне pipeline** | Backend декодирует точный кадр через FFmpeg, QML показывает `Image` и fallback. Это не `QVideoSink`-стрим, а детерминированные стоп-кадры. Нужна ручная проверка на реальном видео. |
| Таймлайн мешал preview | **Готово** | Нижняя timeline-панель удалена, высота отдана A/B viewport. |
| Zoom, pan и fit-to-window | **Готово** | Колесо, drag и double-click/reset реализованы в `ComparisonView.qml`. |
| Settings дублировал анализ | **Готово** | В Settings оставлены provider, cache path/limit, threads и оформление; оперативные параметры находятся в левом rail. |
| Тень и анимация модального окна | **Готово** | `MultiEffect` shadow + opacity/scale transitions 150–180 мс. |
| Случайные консольные окна | **Готово на уровне кода** | Нет `AllocConsole`/`system`; FFmpeg и runtime распаковываются через `QProcess` с `CREATE_NO_WINDOW`. Реальный запуск portable exe надо проверить вручную. |
| Provider выбирается, но не применяется | **Частично** | Выбор передаётся в pose/ReID estimator и проходит preflight; CPU/CUDA negative smoke теперь завершается сразу и не ждёт 120-секундный таймер. CUDA/TensorRT/DML end-to-end зависят от DLL, драйвера и release-архива. После скачивания требуется перезапуск. |
| Кнопка скачать runtime | **Частично** | Кнопка проверяет интернет, release-manifest и fallback на `main/providers/providers.json`, показывает прогресс и атомарно устанавливает bundle. В GitHub пока нет опубликованного `providers.json`, поэтому сетевой сценарий не доказан; рядом добавлена официальная инструкция. |
| Модель выбирается, но неизвестно, работает ли | **Частично** | UI проверяет файл и формат, выбор блокируется на загрузке, runtime получает путь. Нужен реальный ONNX batch-1 и запуск inference на машине пользователя. |
| Автоскачивание моделей из GitHub | **Частично** | `manifest.json`, `.part`, SHA-256, size и local model roots реализованы. Локально экспортированы 15 ONNX и создан `release-models/manifest.json`, но реальный Release ещё не опубликован. |
| Один человек сравнивался с другим | **Частично** | Внутри одного файла работает `trackId`; локальный OSNet body-ReID теперь подключается и проходит inference smoke. Identity benchmark на размеченных людях ещё не выполнен. |
| Матчер сравнивал один кадр со всеми | **Готово в архитектуре** | Окна 2–4 секунды, минимум 8 разных timestamped descriptors, dense coarse sketch из 16 samples и temporal run gate. Выровненный DTW запрещает повторно использовать один и тот же right-frame (`candidate <= previousRight` отбрасывается). Нужен benchmark на размеченном наборе. |
| `static · static` и `mixed · mixed` давали мусор | **Готово для обычного режима** | Motion windows проходят delta/active-transition/range gates; static/static удаляется в motion mode. Явный static mode сохраняет осознанные static-пары. UI убирает двойной `mixed`. |
| Самосравнения и близкие повторы | **Готово** | Scene/track guards, same-source floor 5 с, repeat gaps и NMS/dedup. Порог всё равно нужно калибровать на реальном материале. |
| Результаты сортируются по реальной схожести | **Готово** | Сначала calibrated similarity descending, затем детерминированные tie-breakers. |
| Экспортная папка не выбиралась | **Готово на уровне path handling** | `QUrl::toLocalFile()`/нормализация применяются и в QML, и перед FFmpeg/exporter; создаётся папка с понятной ошибкой. Нужен ручной тест через native FolderDialog. |
| FFmpeg писал `*.mp4.part` и не выбирал контейнер | **Готово на уровне кода** | Временный файл теперь `*.part.mp4`, то есть расширение контейнера сохраняется; прямой FFmpeg smoke создал MP4-файл. Полный клип через GUI на Windows ещё требует ручного сценария. |
| EDL/FCP/AEP убрать, FFmpeg добавить | **Готово в UI** | UI оставляет JSON/CSV/TXT/FFMPEG; legacy API в C++ сохранён для совместимости. |
| Accent color в Settings | **Готово** | Orange/blue/violet/teal сохраняются через `QtCore.Settings` и меняют `Theme.accent`. |
| Прозрачность панелей 25–100% | **Готово на уровне кода** | Диапазон расширен до 25–100%; цветовая альфа откалибрована нелинейно, чтобы 70–80% визуально не выглядели как почти прозрачные панели. |
| Подсказки для параметров анализа | **Готово на уровне кода** | `PfSliderField` показывает app-style `ToolTip` при наведении на label, rail или числовое поле. Визуально на целевом DPI ещё нужно проверить. |
| Тесты не должны попадать в Git | **Готово** | `/tests/` добавлен в `.gitignore`, tracked test files удалены из индекса, локальная папка сохранена. CMake не падает без неё. |
| README проекта и профиля | **Готово** | Проектный README переписан, добавлен copy-ready `PROFILE_README.md`. |

## Проверка matcher

### Что проверяет код

1. Нормализация поз и анатомический penalty по topology/proportions.
2. Motion activity: средняя delta-K, active-transition ratio и trajectory range.
3. Не менее `minTemporalFrames = 8` непустых кадров с возрастающими timestamp.
4. Непрерывная temporal run с cosine threshold.
5. Constrained DTW и dense prefilter вместо endpoint-only сравнения; в descriptor
   добавлена относительная траектория центра/масштаба тела, чтобы нормализация не
   стирала реальное перемещение.
6. Same-source gap floor 5 секунд, scene guard и same-track guard.
7. NMS по overlap и дополнительная дедупликация близких окон.
8. Optional appearance cosine gate, если ReID-эмбеддинги действительно есть.

### Что это не доказывает

Число найденных пар и качество разделения персонажей нельзя подтвердить
синтетическим smoke-тестом. Для release нужны размеченные видео с несколькими
людьми, одинаковыми сценами и намеренно статичными отрезками. До такого
benchmark нельзя честно писать «matcher точный» или обещать конкретный процент
precision/recall.

## Проверка моделей и provider

### Уже есть

- canonical model catalog `yolov8*`, `yolo11*`, `yolo26*`;
- `.pt` не выдаётся за готовый runtime-файл;
- ONNX asset скачивается атомарно и проверяется;
- provider choice доходит до обеих inference-сессий;
- недоступность backend показывается явно, без тихого fallback.

### Фактический локальный прогон 15 сентября 2026

- `D:\YOLO_Download_Project\models` содержит 15 исходных `*.pt`; после установки
  `onnx` скрипт экспорта успешно создал 15 ONNX-файлов и SHA-256 manifest в
  локальном, игнорируемом Git каталоге `D:\Parallel-Finder\release-models`.
- `onnxruntime.dll` из `D:\msys2\ucrt64\bin` загрузился как ORT 1.26.0,
  но на этом ПК доступны только `cpu`; CUDA, TensorRT и DirectML сообщили
  отсутствие устройства/EP. Это не проверка NVIDIA-драйвера с GPU runtime.
- URL `https://github.com/Pozit1vchic/Parallel-Finder/releases/latest/download/providers.json`
  сейчас отвечает HTTP 404; поэтому UI-кнопка runtime корректно показывает
  ошибку, но скачать provider ей пока неоткуда.
- CPU analysis smoke на 30-секундном фрагменте (`1801` кадров) с
  `yolo26m-pose-640-b1.onnx` построил `45` окон и, после подключения
  `person-reid-osnet.onnx`, нашёл `1` пару в `motion`-режиме. Это подтверждает
  прохождение pipeline и temporal gates, но не является accuracy benchmark.
- Для `yolo26m-pose-640-b8.onnx` добавлен настоящий `inferBatch`-путь. На CPU
  приложение автоматически выбирает установленный `b1`-сосед: CPU не должен
  вычислять восемь padding-слотов ради GPU-профиля. На CUDA/TensorRT b8 можно
  использовать как ускоренный профиль, если runtime действительно доступен.
- Прогон большого `Soldier Boy (The Boys) _ Scenepack 4K.mp4` на CPU не
  уложился в текущий 120-секундный headless timeout и был остановлен без
  частичного результата. Это честный сигнал о стоимости полного CPU-прогона,
  а не подтверждение качества matcher; для него нужен GPU-runtime или отдельный
  длительный benchmark с сохранением промежуточного прогресса.

### Не подтверждено внешним окружением

- реальный GitHub Release с `manifest.json`;
- `providers.json` и архивы CUDA/TensorRT/DirectML;
- запуск каждой модели из каталога пользователя;
- identity benchmark body-ReID на размеченных людях;
- совместимость конкретной версии NVIDIA driver + ORT + TensorRT.
- совместимость body-ReID на нескольких камерах/одежде и выбранный threshold.

## UI-аудит

### Закрытые проблемы

- центральное comparison-view больше не делит место с timeline;
- advanced cards находятся внутри вертикального `Flickable`;
- длинные названия имеют wrap/elide только там, где это нужно;
- числовые значения не зашиты в картинку и доступны для ручного ввода;
- Settings/Export имеют overlay, shadow, close policy и focus handling;
- результаты идут единым списком без лишних категорий.

### Осталось принять глазами

- реальный размер окна на 100/125/150% DPI;
- Verdana и длинные локализованные строки;
- открытие advanced-блока на узком rail;
- native FolderDialog на Windows;
- настоящий кадр A/B и состояние ошибки декодера.

## Честная проверка сборки

Последний локальный прогон:

```text
cmake --build build/ucrt64-release --parallel 4   PASS
ctest --test-dir build/ucrt64-release --output-on-failure   3/3 PASS
cmake -S . -B build/no-tests -G Ninja -DBUILD_TESTING=ON   CONFIGURE PASS
```

Эти проверки подтверждают компиляцию, QML loading, smoke lifecycle и реальный
CPU pipeline. Они не заменяют end-to-end GPU, body-ReID, accuracy benchmark и
визуальную проверку на мониторе.

## Следующие приоритеты

1. С авторизованным GitHub-токеном опубликовать Release с локальными ONNX,
   `manifest.json`, `providers.json` и SHA-256. Анонимный GitHub API сейчас
   возвращает `404` для `releases/latest`, то есть публичного Release ещё нет;
   без credentials агент не может безопасно создать его от имени владельца.
2. Включить проверенный OSNet ONNX в release-manifest и прогнать identity
   benchmark на размеченных людях.
3. Собрать небольшой размеченный набор видео и измерить precision/recall,
   static false-positive rate и duplicate rate.
4. Проверить portable-папку без консольного окна на чистой Windows-машине.
5. Провести ручной visual QA на целевых DPI и реальных длинных названиях.
