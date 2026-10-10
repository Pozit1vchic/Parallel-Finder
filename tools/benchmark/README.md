# Benchmark и pipeline smoke

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
./build/ucrt64-release/pf_matcher_probe.exe build/windows.json 10 22
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
