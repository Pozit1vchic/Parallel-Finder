# Parallel Finder

**Локальный инструмент для поиска повторяющихся движений в видео.**

Parallel Finder сравнивает временные фрагменты двух источников, учитывает
траекторию позы, движение, временной контекст и, при наличии модели, внешний
вид человека. Результат — реальные пары `A[t₀…t₁] ↔ B[t₂…t₃]` с кадрами,
таймкодами, оценкой схожести и экспортом.

[![CI](https://github.com/Pozit1vchic/Parallel-Finder/actions/workflows/ci.yml/badge.svg)](https://github.com/Pozit1vchic/Parallel-Finder/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-AGPL--3.0-orange.svg)](LICENSE)
[![Qt](https://img.shields.io/badge/Qt-6.5%2B-41CD52.svg)](https://www.qt.io/)
[![C%2B%2B](https://img.shields.io/badge/C%2B%2B-23-00599C.svg)](https://en.cppreference.com/w/cpp/23)

> **Честный статус.** Проект находится на стадии рабочего прототипа и
> подготовки релиза. Локальные unit/UI/smoke-проверки проходят, но это не
> доказывает работу на каждой видеокарте, реальном GitHub Release или любой
> ONNX-модели. Эти границы описаны ниже.

## Что уже есть

- Qt 6 / QML-интерфейс с тремя рабочими зонами: источники, сравнение, результаты.
- Реальные источники видео, выбор папки, предпросмотр A/B и таймкоды.
- Трекинг всех обнаруженных людей, а не только одного «главного» персонажа.
- Фильтр самосравнений, статики, коротких совпадений и перекрывающихся результатов.
- HNSW-style prefilter → ограниченный DTW → ранжирование.
- Опциональный body-ReID для отсечения совпадений разных людей.
- ONNX Runtime с `auto`, DirectML, CUDA, TensorRT и CPU-провайдерами.
- Кэш признаков `PFCACHE1`, настройки, экспорт JSON/CSV/TXT/EDL/FCPXML/AEP.
- Автоматическое скачивание отсутствующих ONNX-моделей из GitHub Releases.

## Что проект не обещает

- `.pt`-файлы не запускаются напрямую: нужен ONNX-экспорт.
- CUDA и TensorRT не появятся от одного выбора в меню: нужны совместимые
  runtime/DLL и драйверы на машине пользователя.
- Body-ReID не распознаёт личность по лицу. Без OSNet/FastReID ONNX работает
  прозрачный режим `pose-only`.
- Наличие unit-теста не заменяет проверку на настоящем видео и конкретном GPU.
- В репозитории пока нет опубликованного GitHub Release с моделями; поэтому
  сетевое скачивание станет работоспособным после загрузки release assets.

## Как устроен проект

```text
video files
    │
    ▼
VideoDecoder → SceneDetector → PoseEstimator → PersonTracker
                                               │
                         optional Body-ReID ──┘
    │
    ▼
Motion windows → prefilter → temporal checks → DTW → NMS/ranking
    │                                      │
    ├── PFCACHE1                           ├── QML comparison view
    └── exporters                           └── JSON/CSV/TXT/EDL/FCPXML/AEP
```

| Каталог | Ответственность |
| --- | --- |
| `app/` | Тонкая точка входа Windows-приложения |
| `core/` | Декодирование, сцены, трекинг, matcher, ranking, jobs |
| `gpu/` | ONNX Runtime, провайдеры, pose и body-ReID |
| `services/` | ModelStore, PFCACHE1, settings, thumbnails, ffmpeg cuts |
| `exporters/` | JSON, CSV, TXT, EDL, FCPXML, AEP/JSX |
| `ui/` | QML-компоненты и C++ bridge |
| `tests/` | GoogleTest, QTest и smoke-проверки |
| `tools/model_export/` | Экспорт `.pt` → ONNX и генерация release manifest |
| `docs/` | Контракты, решения, аудит и release-инструкции |

`core/` и `exporters/` не зависят от Qt. Это проверяется отдельным
`pfcore_qt_ban`-guard при сборке.

## Быстрый старт для разработки

### Требования

- Windows 10/11;
- MSYS2 **UCRT64** — не MINGW64 и не CLANG64;
- CMake 3.25+, Ninja, GCC;
- Qt 6.5+ (`base`, `declarative`, `shadertools`);
- FFmpeg;
- ONNX Runtime;
- GoogleTest для тестов.

Установить базовые пакеты в MSYS2 UCRT64:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-shadertools \
  mingw-w64-ucrt-x86_64-gtest \
  mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-onnxruntime
```

### Сборка и проверки

Из корня репозитория:

```bash
cmake --preset ucrt64-release
cmake --build --preset ucrt64-release
ctest --preset ucrt64-release --output-on-failure
```

Debug-вариант:

```bash
cmake --preset ucrt64-debug
cmake --build --preset ucrt64-debug
ctest --preset ucrt64-debug --output-on-failure
```

Запуск собранного приложения:

```text
build/ucrt64-release/ParallelFinder.exe
```

`QT_QPA_PLATFORM=offscreen` используется только тестами. Такой тест проверяет
загрузку QML и базовый жизненный цикл, но не заменяет просмотр интерфейса на
целевом мониторе.

## Модели: от ваших `.pt` до автоматической загрузки

Ваш исходный каталог:

```text
D:\YOLO\_Download_Project\models
```

Исходные `.pt` нужны только для подготовки release. В корне проекта есть
скрипт, который экспортирует все `*pose*.pt` в статический batch-1 ONNX
640×640, считает SHA-256 и создаёт `manifest.json`:

```powershell
$env:PYTHONPATH = 'D:\PythonLibs'

python D:\Parallel-Finder\tools\model_export\export_pose_release.py `
  --input-dir 'D:\YOLO\_Download_Project\models' `
  --output-dir 'D:\Parallel-Finder\release-models'
```

Затем создайте GitHub Release в
`https://github.com/Pozit1vchic/Parallel-Finder/releases` и загрузите **все**
файлы из `release-models/` как assets одного релиза:

```text
yolov8*-pose.onnx
yolo11*-pose.onnx
yolo26*-pose.onnx
manifest.json
```

Приложение получает manifest по адресу:

```text
https://github.com/Pozit1vchic/Parallel-Finder/releases/latest/download/manifest.json
```

При выборе отсутствующей модели UI показывает `↓ скачать`, загрузка идёт с
прогрессом, а готовый файл устанавливается в:

```text
%LocalAppData%\ParallelFinder\models
```

Класть модель рядом с exe не нужно. Для локальной разработки можно использовать:

```powershell
$env:PF_MODEL_ROOT = 'D:\PF_CUDA\models'
$env:PF_MODEL_PATH = 'D:\PF_CUDA\models\yolo26m-pose-640-b1.onnx'
```

Полная спецификация: [`docs/models.md`](docs/models.md) и
[`tools/model_export/README.md`](tools/model_export/README.md).

## Провайдеры вычислений

| Выбор | Для чего | Условие |
| --- | --- | --- |
| `auto` | Выбрать лучший доступный backend | Probe ONNX Runtime |
| `dml` | DirectML для AMD/Intel/NVIDIA | DirectML EP и совместимый runtime |
| `cuda` | CUDA для NVIDIA | CUDA EP, DLL и драйвер |
| `tensorrt` | TensorRT для NVIDIA | TensorRT + CUDA + совместимый engine/runtime |
| `cpu` | Универсальный fallback | Только ONNX Runtime CPU |

При ручном выборе недоступного backend анализ останавливается с причиной.
Тихого переключения на CPU нет.

## Как matcher отсекает мусор

Одиночное похожее изображение не считается параллелью. Перед финальным score:

1. окно должно иметь достаточную длину;
2. должна быть непрерывная серия похожих кадров;
3. обе стороны должны иметь реальное движение, а не только detector jitter;
4. самосравнение, близкие интервалы и та же сцена отбрасываются;
5. внутри одного исходника проверяется одинаковый `trackId`;
6. перекрывающиеся результаты проходят deduplication/NMS;
7. при доступном ReID применяется дополнительная проверка внешнего вида.

Пороговые значения можно менять в Advanced-настройках основного окна.

## Что именно проверяют тесты

| Проверка | Покрывает | Не доказывает |
| --- | --- | --- |
| `pf_tests` | core, services, cache, exporter, matcher | качество на любом реальном фильме |
| `pfui_tests` | загрузку QML-модуля и bridge | визуальную полировку на каждом DPI |
| `app_smoke` | старт приложения в тестовом окружении | полный Windows portable bundle |
| CI | Debug/Release сборку в MSYS2 UCRT64 | CUDA/TensorRT на вашей машине |

Реальный release-чеклист:

- [ ] опубликован GitHub Release с ONNX и `manifest.json`;
- [ ] отсутствующая модель скачивается до 100%;
- [ ] повторный выбор использует локальный файл без сети;
- [ ] повреждённый файл отклоняется по SHA-256;
- [ ] проверены `dml`, `cuda`, `tensorrt`, `cpu` на целевых системах;
- [ ] проверено хотя бы одно короткое и одно длинное видео;
- [ ] body-ReID проверен с реальной OSNet/FastReID ONNX-моделью;
- [ ] собрана portable-папка с Qt/FFmpeg/ORT DLL.

## Документация

- [`docs/design.md`](docs/design.md) — визуальный контракт интерфейса.
- [`docs/ui-audit.md`](docs/ui-audit.md) — аудит QML и остаточные UI-ограничения.
- [`docs/decisions.md`](docs/decisions.md) — архитектурные решения и параметры.
- [`docs/models.md`](docs/models.md) — каталог моделей и GitHub Release workflow.
- [`docs/full-audit.md`](docs/full-audit.md) — сводный технический аудит.
- [`docs/model-audit.md`](docs/model-audit.md) — аудит model/provider/cache pipeline.
- [`docs/provider-runtime.md`](docs/provider-runtime.md) — формат release-манифеста
  и установка CUDA/TensorRT/DirectML runtime.
- [`docs/engineering-notes.md`](docs/engineering-notes.md) — инженерные правила.

## Лицензирование

- Код проекта: [AGPL-3.0](LICENSE).
- YOLO-pose веса: не входят в Git и должны распространяться с соблюдением их
  лицензии и условий исходного проекта.
- Qt: динамическая линковка по LGPL-варианту поставки Qt.
- ONNX Runtime: MIT.
- FFmpeg-сборка может включать GPL-компоненты (`x264`, `x265`, `xvid`).

Перед распространением portable-релиза проверьте состав DLL и лицензий.
