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
DLL ONNX Runtime, успешное завершение анализа и различие четырёх режимов.

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
| `clips` | окна клипов длиной несколько секунд |
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
