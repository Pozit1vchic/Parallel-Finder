# Benchmark и pipeline smoke

`pf_classifier_interval_bench [iterations]` измеряет только вызовы геометрической
классификации заданного интервала: 240 синтетических наблюдений, 17 суставов,
120 кадров внутри полуоткрытого диапазона. Соберите цель через CMake и запускайте
старый/новый EXE по очереди в одинаковой Release-конфигурации, меняя порядок
между проходами. JSON содержит время, число вызовов и контрольную сумму.
Этот замер не включает декодирование, inference, поиск или экспорт и не измеряет
точность модели. Контрольная сумма должна совпадать, прежде чем сравнивать время.

`evaluate_quality.py` дополнительно выводит per-class precision/recall/F1,
macro-F1 по объединению истинных и предсказанных классов и `unclassifiedRate`.
Предсказанный `unknown` остаётся в знаменателе; эти показатели нельзя выдавать
за независимую проверку, если примеры уже использовались при разработке.

Проверка контрольных пар принимает JSON benchmark-отчёт или stdout завершённого анализа с `PF_ANALYSIS_JSON=1`:

```powershell
./tools/benchmark/check_reference_pairs.ps1 -ReportPath build/recall-fixed-soldier.out.log
./tools/benchmark/check_reference_pairs.ps1 -ReportPath build/recall-fixed-dean.out.log -ReferencePath tools/benchmark/dean-reference.json
```

Отсутствующий положительный якорь или найденная отмеченная ложная пара приводит к ошибке проверки. Это контрольная выборка, не оценка общей точности. Для короткой диагностики заполнения лимита можно включить `PF_DEBUG_SELECTION=1`; подробный `PF_DEBUG_MATCHER` для этой задачи не нужен.

Репозиторий не подменяет качество матчера красивым процентом. Для настоящего
precision/recall нужны размеченные видео и эталонные интервалы. Скрипт рядом
проверяет воспроизводимый инженерный минимум: выбранный ONNX-файл, выбранную
DLL ONNX Runtime и успешное завершение трёх доступных режимов анализа.

```powershell
powershell -ExecutionPolicy Bypass -File tools/benchmark/run_analysis_smoke.ps1 `
  -Video 'D:\Загрузки 2\3b34e39b6ed2ba91.mp4' `
  -ReIdModel 'D:\PF_CUDA\models\person-reid-osnet.onnx'
```

Пути можно переопределить параметрами `-Exe`, `-Model` и `-OrtDll`. Режимы:

| Режим | Что проверяет |
| --- | --- |
| `motion` | последовательности с движением и temporal gating |
| `static` | отдельный явный аудит статичных кадров |
| `combined` | motion + static в одном запуске |

Скрипт использует `PF_ANALYSIS_MODE` только для headless smoke-запуска и не
перезаписывает пользовательские настройки. Результаты прогона не добавляются
в Git: это локальная диагностика, а не тестовый fixture.

Для диагностики нулевого результата можно временно включить счётчики без
изменения пользовательского интерфейса:

```powershell
$env:PF_DEBUG_ANALYSIS = '1' # окна и число детекций
$env:PF_DEBUG_MATCHER = '1'  # gates, temporal run, accepted pairs
$env:PF_DEBUG_POSE = '1'     # форма выхода YOLO и confidence
```

Эти переменные не включаются по умолчанию и не должны использоваться как
метрика precision/recall.

## Аудит отдельных пропусков без повторного YOLO

`PF_DEBUG_POSES_JSON` задаёт абсолютный путь для диагностического JSON с
исходными наблюдениями, сценами и признаками личности. Храните его только в
локальном `build`: он содержит пути исходников и биометрические признаки и
не предназначен для публикации. Индексы окон в нём начинаются с нуля.

```powershell
$env:PF_DEBUG_POSES_JSON = 'D:\Parallel-Finder\build\windows.json'
# Выполните run_analysis_smoke.ps1 с нужным видео и моделями.
$env:PF_DEBUG_MATCHER = '1'
./build/ucrt64-release/tools/pf_matcher_probe.exe build/windows.json 10 22
```

Probe использует реальный код сравнения с параметрами быстрого desktop-профиля
(сходство .70, временной вес .10, body identity .76, артикуляция .82), но не
весь отбор выдачи/NMS и не произвольные текущие настройки пользователя.
Он помогает отличить отсутствие пригодного окна, отказ личности/геометрии и
потерю кандидата при отборе; сам по себе не измеряет качество видео.

`PF_EXPANDED_SEARCH=0/1` и `PF_QUALITY_PROFILE=fast/medium/maximum` позволяют
сравнить ширину поиска и плотность наблюдений без сохранения настроек. Для
сравнения времени отмечайте холодный/полный cache hit и число заново
отрендеренных превью. Правила контрольных пар и ограничения измерений описаны в
[методике проверки матчера](../../docs/matcher-validation.md).

## Полный performance-аудит cold/warm

`run_pipeline_audit.ps1` требует явные `-Video`, `-Exe`, `-Model`, `-OrtDll`,
`-ReIdModel`; фиксирует SHA-256 assets, создаёт **новый** observation/PNG cache
для каждого cold и запускает paired warm только на этом кеше. Проверяет cache
hit и совпадение всех полей результатов, кроме session-local PNG paths.
`-TraceInference`, `-TraceOrtNodes` и `-SampleGpu` нужны для диагностики, не для
заявленного ускорения. `-EngineCache` задаёт существующий каталог движков TRT.

`run_pipeline_ab.ps1` принимает два EXE (`-BeforeExe`, `-AfterExe`), те же
фиксированные assets и `-Provider`. Запускает A/B последовательно с чередованием
порядка, сохраняет `summary.json` и проверяет все поля **и SHA-256 каждого `.pfc`**.
`compare_pipeline_audit.ps1 -BeforeRoot ... -AfterRoot ... -CompareCacheBytes`
повторяет эту строгую проверку; пустые/неполные каталоги не считаются успехом.

`profile_gpu_pipeline.ps1` использует установленный Nsight Systems, пишет
диагностический trace под `build`; его timings не используются как обычное
время анализа. Отдельные поля `elapsedMs` и `processWallMs` позволяют не скрывать
создание процесса и проверку runtime за быстрым cache-hit analysis.
Для nested Windows paths используйте короткий `-OutputDirectory`: нативный
cache stream не гарантирует extended-length paths, и слишком длинный путь
теперь отклоняется до измерения. `summarize_gpu_activity.sql` объединяет
перекрывающиеся CUDA intervals в Nsight SQLite; coverage не означает SM occupancy.
Правила проверки качества и границы утверждений — в
[методике регрессий](../../docs/matcher-validation.md); текущие замеры — в [AUDIT.md](../../AUDIT.md).

## Независимая проверка качества и ошибок личности

[Методика проверки](../../docs/matcher-validation.md) отделяет
frozen recall от проверки всей выдачи; успешный smoke не означает precision.
`evaluate_visual_reference.ps1 -SamePersonOnly` исключает явно размеченных
разных исполнителей по выбранной пользователем области, не изменяя frozen JSON.

`pf_matcher_probe --all [params.json]` воспроизводит retrieval, exact comparison,
selection и ranker; `--batch pairs.json [params.json]` — отдельные сравнения.
В config `maxComparisonThreads=1` принудительно задаёт serial, `0` — bounded
auto. Сравнивайте все поля результатов, не только число карточек.

Для наблюдений до усреднения identity prototype включите на **cold** запуске:

```powershell
$env:PF_DEBUG_APPEARANCE_JSON = 'D:\Parallel-Finder\build\appearance-samples.json'
$env:PF_DEBUG_POSES_JSON = 'D:\Parallel-Finder\build\appearance-windows.json'
# Выполните run_analysis_smoke.ps1 с явными моделями и новым коротким cache path.
./tools/benchmark/inspect_appearance_observations.ps1 `
    -ObservationsPath build/appearance-samples.json `
    -WindowsPath build/appearance-windows.json -WindowIndex 10,22 `
    -OutputPath build/appearance-consistency.json
Remove-Item Env:PF_DEBUG_APPEARANCE_JSON,Env:PF_DEBUG_POSES_JSON
```

Appearance audit сохраняет track, реальные timestamps, person crop box,
keypoints и face/body vectors. Cache hit не содержит исходных per-crop samples
и не создаёт такой файл. Несколько входов получают суффикс номера файла.
Существующий audit не перезаписывается. Это биометрическая локальная диагностика,
её нельзя коммитить или публиковать; cosine внутри prototype не доказывает,
что модель выбрала лицо нужного человека.

`render_pose_windows.ps1` накладывает реальные detector points на исходные
кадры; `render_returned_pairs.ps1` создаёт representative contact sheets.
Ни один из этих инструментов не создаёт независимую ground truth.
`PF_DEBUG_SCENE_SCORES=1` показывает score соседних scene samples, их медианы
и robust spread; он выключен в обычном запуске.

Для проверки конкретного face crop используйте `extract_appearance_samples.ps1`
с выбранными track/time, затем `pf_face_probe samples.json yunet.onnx sface.onnx`.
Probe возвращает detector landmarks, канонический transform и features той же
реализации. `compare_face_alignment.py` сравнивает тот же detector output с
эталонным alignCrop OpenCV и сохраняет две версии 112×112 рядом.
Python/OpenCV нужны **только для локального аудита**, не для приложения:
при необходимости установите их в отдельное окружение под `build`, не в runtime
пользователя. Reference cosine не является новой разметкой identity.

Config `minHeadFaceSimilarity` позволяет изолированно проверить более строгую
identity для head-only comparisons, не меняя общий face/body/pose threshold.
Перед claim об улучшении сравните новые/пропавшие пары и top-N, а не только count.

`run_matcher_ab.ps1` делает изолированные replay двух config на одинаковых
window dumps, чередует A/B порядок, сохраняет SHA-256 входов и проверяет
детерминированность всех полей каждой версии. Для сравнения разных binaries
передайте `-ProbeB`; иначе обе версии используют `-Probe`. Отчёт измеряет
end-to-end `findAllPairs`, не parsing, inference, previews или ranking.
Разница configs/EXE может менять quality: `allResultFieldsIdentical=false`
не является улучшением и требует отдельной визуальной оценки.

При извлечении последовательности по предсказанной паре передавайте
`extract_visual_sequence.ps1 -PredictedPairReview`: provenance тогда явно
указывает `matcherIndependent=false`. Эти материалы не заменяют frozen truth.

## Порядок подготовки превью

Опциональная проверка `VideoDecoder.RealPreviewOrderingPreservesFramesAndReportsDecodeWork`
берёт пары из уже созданного отчёта, повторно декодирует исходные видео в старом
и сгруппированном по файлу/времени порядке и проверяет SHA-256 изображений и
временные метки. Она также выводит число декодированных кадров и медиану времени
трёх чередующихся проходов; это замер только подготовки превью, не полного
анализа. Используй отчёт с реальными файлами из `D:\for_tests_pf`:

```powershell
$env:PF_PREVIEW_ORDER_REPORT = 'D:\Parallel-Finder\build\source-all-seven-20261018\report.json'
$env:PF_PREVIEW_ORDER_LIMIT = '48'
ctest --test-dir build/ucrt64-release -R '^pf_tests$' --output-on-failure
Remove-Item Env:PF_PREVIEW_ORDER_REPORT, Env:PF_PREVIEW_ORDER_LIMIT
```

Без `PF_PREVIEW_ORDER_REPORT` этот тест пропускается и обычный набор не открывает
видеофайлы пользователя.

## Независимые KTH controls и маленькие входные кадры

Протокол и измеренные ограничения: [отчёт KTH](../../docs/low-resolution-pose-validation.md).
Разделение по людям зафиксировано в `kth-validation-plan.json`; правила walk/run —
в `kth-semantic-policy.json`. Holdout запрещено использовать для подбора порогов.
Это шесть действий на статичной камере, а не разметка монтажных параллелей.
Условия официального источника — некоммерческое использование. Видео и
сгенерированные observations остаются локально в игнорируемом `build`.

Загрузчик читает только нужные ZIP members через HTTP Range, проверяет CRC,
размер и полноту запланированного split; сохраняет SHA-256 и исходные границы.
Пример одного split (повтори отдельно для tuning/development/holdout):

```powershell
python tools/benchmark/fetch_kth_controls.py --destination build/goal-kth --split holdout
$env:PATH = 'D:\msys2\ucrt64\bin;' + $env:PATH
# Для CUDA явно укажи установленный runtime и каталог его зависимостей:
$env:PF_ORT_DLL = 'C:\path\to\cuda\onnxruntime.dll'
$env:PF_PROVIDER_ROOT = Split-Path -Parent $env:PF_ORT_DLL
$env:PATH = $env:PF_PROVIDER_ROOT + ';' + $env:PATH
build/ucrt64-release/tools/pf_pose_observation_probe.exe `
    release-models/yolo26m-pose.onnx build/goal-kth/manifest-holdout.json `
    build/goal-kth-poses-after-holdout 12 cuda 4
python tools/benchmark/evaluate_kth_controls.py `
    --dataset-manifest build/goal-kth/manifest-holdout.json `
    --observations build/goal-kth-poses-after-holdout `
    --probe build/ucrt64-release/tools/pf_matcher_probe.exe `
    --output build/goal-kth-after-holdout-evaluation
```

Для A/B сохраняй **разные** extractor executables до/после исправления, одинаковые
модель/provider/fps/threads и отдельные output directories. Для сравнения только
матчера передавай одинаковые observations. Эксперимент с изменением preprocessing
по определению меняет observations: он не изолирует эффект одного matcher fix.
Не пересоздавай baseline текущим бинарным файлом. Исторические исходники
`MovementClassifier.cpp`, `MotionIndex.cpp`, `MotionRanker.cpp` доступны в ревизии
`6374789`; benchmark adapter для точных границ и supported runs должен совпадать
между сторонами. Все evaluation outputs сохраняют hash probe и входных контрактов.

Проверка существующей AVA-модели использует только первый опубликованный интервал
каждого видео. Подготовка не смотрит на predicted action; ROI берётся из реальной
single-person detection. Это явно помеченная dataset selection, без доказательства
выбора нужной личности в многолюдном видео.

```powershell
python tools/benchmark/prepare_kth_action_cases.py `
    --dataset-manifest build/goal-kth/manifest-holdout.json `
    --observations build/goal-kth-poses-after-holdout `
    --output build/goal-kth-ava-holdout
$actionManifest = Get-Content build/goal-kth-ava-holdout/manifest.json -Raw | ConvertFrom-Json
foreach ($batch in $actionManifest.batches) {
    $batchName = [IO.Path]::GetFileNameWithoutExtension($batch).Replace('cases-', '')
    build/ucrt64-release/tools/pf_video_action_probe.exe `
        "build/goal-kth-ava-holdout/$batch" models/ava-slowfast-r50.onnx cuda `
        "build/goal-kth-ava-holdout/predictions-$batchName.json"
    if ($LASTEXITCODE -ne 0) { throw "Probe failed: $batch" }
}
python tools/benchmark/evaluate_kth_video_actions.py `
    --cases-root build/goal-kth-ava-holdout `
    --output build/goal-kth-ava-holdout-evaluation.json
```

Probe не перезаписывает готовый output. Для воспроизведения используй новый
каталог, сверяй model hash, source hashes и policy hash. Оценщик удерживает
недоступные случаи в знаменателе. Forward/reverse/repeated-midpoint — три вида
входа одной модели, а не три новые модели; высокие raw scores не являются
калиброванными вероятностями. Поиск по 80 scores здесь диагностический, с известным
человеком и cross-scenario кандидатами; production reranker из него автоматически
не получается. `pf_kth_controls` проверяет загрузку, границы, отсутствие утечек
split, сохранение пропусков и зафиксированные semantic gates без скачивания видео.

## Полные реальные серии и изоляция настроек

[Протокол серий и текущий аудит кеширования](../../docs/chernobyl-cache-validation.md)
отделяет полные эпизоды от маленьких контрольных отрывков. Split зафиксирован в
`chernobyl-validation-plan.json`. Сами серии, извлечённые кадры, observations и
токены не коммитятся и не включаются в релиз.

`PF_BENCHMARK_SETTINGS_PATH` работает только при `--pf-analysis-smoke`: он задаёт
отдельный JSON настроек, не изменяя путь обычного приложения. Явный путь
`SettingsStore` также сохраняет своё поведение. `PF_SMOKE_ALL_PEOPLE=1` вызывает
существующий `analyzeAllPeople` и использует полную ветку UI для всех персонажей,
включая character artifacts. Без этого флага диагностический запуск сохраняет
прежнюю политику выбора одного человека.

Для сборки сравнимой исходной версии используй отдельный checkout `6374789`
и `baseline-6374789-smoke.patch`. Он добавляет только доступ к одинаковым
настройкам и существующему workflow; не переносит исправленные классификатор,
ранкер или preprocessing в baseline. SHA-256 каждого реально запущенного EXE
обязателен в контракте.

`run_pipeline_comparison.ps1` запускает чередующиеся before/after повторы,
создаёт отдельный профиль и новый кеш для каждой версии/повтора, затем запускает
новый процесс с тёплым кешем. `-AllPeople` выбирает полный соответствующий режим.
Холодным называется пустой кеш приложения: файловый кеш Windows не сбрасывается.
Нельзя писать «warm hit», пока диагностические счётчики не подтвердили чтение
совместимых observations. Не объединяй результаты разных mode/quality/contracts.

Для длинных серий запускай `run_analysis_smoke.ps1` с явным `-TimeoutSec`, одним
режимом в `-Modes`, отдельным `-ReportPath` и `-ResourceTelemetry`. Дополнительный
скрытый процесс `monitor_process_resources.ps1` сохраняет JSONL: CPU time, live
working/private memory, логические process I/O, per-process WDDM dedicated/shared
GPU allocations и отдельные engine utilization. Общую память устройства нельзя
подставлять вместо памяти процесса, а проценты разных engines нельзя просто
складывать. Частота CIM-снимков ограничена; кратковременные пики могут быть
пропущены. Зафиксируй одинаковую телеметрию и внешний фон на обеих версиях.
