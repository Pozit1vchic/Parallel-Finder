# Benchmark и pipeline smoke

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
