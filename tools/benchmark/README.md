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
отрендеренных превью. Устойчивые воспроизводимые выводы RC.14 и оставшиеся
пропуски описаны в [отчёте](../../docs/rc14-matcher-cache-audit.md).
