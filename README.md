# Parallel Finder

<p align="center">
  <strong>Поиск повторяющихся движений между видео</strong><br>
  <sub>Локальное Windows-приложение для монтажа, раскадровки и поиска параллелей</sub>
</p>

<p align="center">
  <a href="https://github.com/Pozit1vchic/Parallel-Finder"><img src="https://img.shields.io/badge/status-working%20prototype-d97757?style=flat-square" alt="Working prototype"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-7c9885?style=flat-square" alt="AGPL-3.0"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-5d8dde?style=flat-square" alt="C++23">
  <img src="https://img.shields.io/badge/Qt-6-41a675?style=flat-square" alt="Qt 6">
  <img src="https://img.shields.io/badge/platform-Windows-5d8dde?style=flat-square" alt="Windows">
</p>

> **Parallel Finder сравнивает не случайные картинки, а временные серии поз.**
> Поэтому результат — это пара движений A/B с таймкодами, треком человека и
> понятным уровнем уверенности, а не безымянные «98% похожести».

<p align="center">
  <a href="docs/full-audit.md">Полный аудит</a> ·
  <a href="docs/models.md">Модели</a> ·
  <a href="docs/provider-runtime.md">GPU-runtime</a> ·
  <a href="PROFILE_README.md">README профиля</a>
</p>

---

## Зачем это нужно

Обычный поиск похожих кадров легко принимает за параллель неподвижного
персонажа или один и тот же стоп-кадр. Parallel Finder строит временные окна,
сравнивает последовательности поз, учитывает смену сцен и треки людей, а затем
убирает перекрывающиеся и слишком близкие совпадения.

Приложение работает локально: видео, кадры, позы и результаты не отправляются
в облако.

## Что уже есть

| Область | Реализовано |
| --- | --- |
| Рабочее пространство | Источники, сравнение A/B, результаты и адаптивный QML-layout |
| Видео | Точный стоп-кадр по таймкоду, fallback при недоступном preview, zoom/pan/fit-to-window |
| Анализ | YOLO-pose, детектор сцен, multi-person tracking, временные окна, DTW и NMS |
| Режимы | Движение, статические кадры, клипы 4 секунды, комбинированный режим |
| Идентичность | `trackId` внутри файла и необязательный body-ReID gate между файлами |
| Runtime | Auto, DirectML, CUDA, TensorRT и CPU с preflight-проверкой |
| Модели | Индикатор `✓ установлена` / `↓ скачать`, `.part`, SHA-256, атомарная установка |
| Экспорт | JSON, CSV, TXT и FFmpeg MP4-нарезка выбранных сцен |
| Кэш | Versioned `PFCACHE1`, ключи по модели, provider, quality и режиму анализа |

Это рабочий прототип, а не обещание одинакового качества на любой видеокарте,
драйвере и исходнике. Ограничения собраны в [полном аудите](docs/full-audit.md).

## Быстрый сценарий

1. Добавьте один или несколько роликов через «Источники».
2. Выберите профиль точности и состав анализа.
3. При необходимости раскройте «Дополнительные настройки».
4. Запустите анализ.
5. Нажмите пару справа, чтобы открыть стоп-кадры A/B.
6. Отметьте результаты и выберите JSON/CSV/TXT или FFmpeg для нарезки.

### Режимы анализа

| Режим | Поведение |
| --- | --- |
| **Движение** | Окна примерно по 2,5 секунды; обе стороны обязаны содержать реальное движение суставов. |
| **Статические кадры** | Временные серии примерно по 2 секунды без motion-gate — для осознанного поиска похожих поз. |
| **Клипы 4 с** | Окна около 4 секунд для более длинных монтажных эпизодов. |
| **Движение + кадры** | Два независимых прохода; статические и движущиеся окна между собой не смешиваются. |

Во всех режимах одиночный кадр не считается результатом: окно должно содержать
много разных временных сэмплов и устойчивую серию похожих кадров.

## Модели и автоматическая загрузка

Приложение ожидает **ONNX**, а не `.pt`. Исходные веса можно держать в:

```text
D:\YOLO_Download_Project\models
```

Для runtime приложение проверяет в первую очередь:

```text
%LocalAppData%\ParallelFinder\models
```

Также поддерживаются каталог рядом с exe, `PF_MODEL_PATH`, `PF_MODEL_ROOT` и
`D:\PF_CUDA\models`. Выбор отсутствующей модели показывает кнопку/статус
скачивания. Сначала читается HTTPS `manifest.json`, затем файл скачивается в
`.part`, проверяется размер и SHA-256 и только после этого переименовывается.

Манифест последнего релиза:

```text
https://github.com/Pozit1vchic/Parallel-Finder/releases/latest/download/manifest.json
```

Подготовка ONNX-релиза из `.pt`:

```powershell
python D:\Parallel-Finder\tools\model_export\export_pose_release.py `
  --input-dir 'D:\YOLO_Download_Project\models' `
  --output-dir 'D:\Parallel-Finder\release-models'
```

Подробности: [`docs/models.md`](docs/models.md) и
[`tools/model_export/README.md`](tools/model_export/README.md).

## Провайдеры вычислений

| Провайдер | Целевое железо | Важное условие |
| --- | --- | --- |
| **Auto** | лучший доступный backend | выполняется probe и выбирается совместимый путь |
| **DirectML** | AMD / Intel / NVIDIA | нужен runtime с DirectML EP |
| **CUDA** | NVIDIA | нужны CUDA EP, драйвер и совместимые DLL |
| **TensorRT** | NVIDIA, высокая производительность | нужны TensorRT + CUDA + совместимый ORT |
| **CPU** | любой компьютер | fallback без GPU |

Ручной выбор провайдера действительно передаётся в `PoseEstimator` и
`ReIdEstimator`. Если runtime недоступен, интерфейс показывает причину и не
подменяет выбор молча на CPU. Скачанный runtime применяется после перезапуска,
потому что DLL ONNX Runtime нельзя безопасно заменить внутри уже запущенного
процесса.

## Как устроен matcher

```text
video → decode → scenes → YOLO-pose → trackId → optional body-ReID
                                      ↓
                         temporal windows (не один кадр)
                                      ↓
            candidate index → cosine/anatomy → constrained DTW
                                      ↓
                         gap rules → NMS → ranking
```

В обычном режиме matcher отбрасывает статичные окна, требует минимум 12
различных временных дескрипторов и устойчивую последовательность похожих
кадров. Внутри одного файла учитываются `trackId`, scene boundary и минимальный
зазор. Между разными файлами идентичность усиливается body-ReID, если модель
действительно установлена и успешно запустилась.

## Сборка

### Требования

- Windows 10/11;
- MSYS2 UCRT64;
- CMake 3.25+, Ninja, GCC;
- Qt 6.5+ (`base`, `declarative`, `shadertools`);
- FFmpeg и ONNX Runtime.

```bash
cmake --preset ucrt64-release
cmake --build --preset ucrt64-release
```

Готовый exe после локальной сборки:

```text
D:\Parallel-Finder\build\ucrt64-release\ParallelFinder.exe
```

Папка `tests/` намеренно не входит в Git: она остаётся в checkout разработчика
для локального QA и игнорируется `.gitignore`. Если она есть на машине:

```bash
cmake --preset ucrt64-release -DBUILD_TESTING=ON
cmake --build --preset ucrt64-release
ctest --test-dir build/ucrt64-release --output-on-failure
```

Smoke-запуск приложения:

```text
D:\Parallel-Finder\build\ucrt64-release\ParallelFinder.exe --pf-smoke
```

Последний локальный прогон подтвердил сборку, QML smoke и core-проверки: **3/3**.
Это не заменяет проверку настоящего видео на целевых GPU.

Для воспроизводимой проверки реального видео по всем режимам используйте
[`tools/benchmark/run_analysis_smoke.ps1`](tools/benchmark/run_analysis_smoke.ps1).
Скрипт явно получает модель и `onnxruntime.dll`, затем запускает `motion`,
`static`, `clips` и `combined`. Precision/recall без размеченного датасета
README намеренно не обещает.

## Карта репозитория

```text
app/                 запуск Windows-приложения
core/                декодер, сцены, tracking, matcher, ranking
gpu/                 ONNX Runtime, pose и body-ReID
services/            кэш, model store, provider store, FFmpeg cuts
exporters/           JSON, CSV, TXT и legacy-форматы API
ui/qml/              интерфейс, preview и overlays
ui/src/              C++ bridge для QML
docs/                аудит, дизайн-контракт и инструкции релиза
tools/model_export/  подготовка ONNX и manifest
PROFILE_README.md    готовый README для GitHub-профиля
```

## Честные ограничения

- В репозитории пока нет опубликованного GitHub Release с реальными ONNX и
  provider-архивами, поэтому сетевое скачивание нельзя считать end-to-end
  подтверждённым.
- В текущем окружении не было совместимой OSNet/FastReID ONNX-модели, поэтому
  body-ReID проверен на уровне wiring и gate, но не на реальном identity-set.
- `trackId` надёжно различает людей внутри одного файла; между разными видео
  без body-ReID приложение не может доказать, что это один и тот же человек.
- Offscreen smoke проверяет загрузку QML, но не заменяет ручную проверку DPI,
  шрифтов, overlay и реального кадра на целевом мониторе.

Подробная таблица «что из замечаний закрыто» находится в
[`docs/full-audit.md`](docs/full-audit.md).

## Документация

- [`docs/full-audit.md`](docs/full-audit.md) — актуальный аудит по замечаниям;
- [`docs/ui-audit.md`](docs/ui-audit.md) — QML/layout и остаточные ограничения;
- [`docs/models.md`](docs/models.md) — модели, manifest и GitHub Releases;
- [`docs/provider-runtime.md`](docs/provider-runtime.md) — GPU-runtime;
- [`docs/design.md`](docs/design.md) и [`DESIGN.md`](DESIGN.md) — дизайн-контракт;
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — правила изменений.

## Лицензия

Код проекта распространяется по [AGPL-3.0](LICENSE). Лицензии весов,
ONNX Runtime, Qt, FFmpeg и подключаемых DLL нужно проверять отдельно перед
публикацией portable-релиза.
