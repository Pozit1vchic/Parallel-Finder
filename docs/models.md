# Модели YOLO-pose

Parallel Finder запускает модели ONNX с выходом YOLO-pose (17 keypoints), а не
исходные веса Ultralytics `.pt`. Поэтому файлы из `D:\YOLO_Download_Project\models`
нужно сначала экспортировать в ONNX с размером входа 640, затем положить под
одним из поддерживаемых имён:

```
yolov8n-pose.onnx  yolov8s-pose.onnx  yolov8m-pose.onnx  yolov8l-pose.onnx  yolov8x-pose.onnx
yolo11n-pose.onnx  yolo11s-pose.onnx  yolo11m-pose.onnx  yolo11l-pose.onnx  yolo11x-pose.onnx
yolo26n-pose.onnx  yolo26s-pose.onnx  yolo26m-pose.onnx  yolo26l-pose.onnx  yolo26x-pose.onnx
```

Есть два рабочих места (приоритет сверху вниз):

1. `models` рядом с `ParallelFinder.exe` — удобно для переносимой папки и
   релиза.
2. `%LocalAppData%\ParallelFinder\models` — постоянное пользовательское
   хранилище, куда приложение также скачивает выбранную модель из GitHub
   Releases.

Для разработки можно не копировать файлы, а задать переменную окружения
`PF_MODEL_ROOT` на каталог с ONNX-файлами. `PF_MODEL_PATH` задаёт конкретный
файл и имеет более высокий приоритет. Если файл найден, каталог показывает
`✓ установлена`; если нет — `↓ скачать`, и выбор запускает загрузку в реальном
времени в `%LocalAppData%\ParallelFinder\models`. Прогресс отображается под
селектором, а после загрузки manifest-пакета проверяются размер и SHA-256.

## Release-пакет из `D:\YOLO_Download_Project\models`

`.pt` из этой папки — исходные веса, их нельзя передать ONNX Runtime напрямую.
Подготовьте пакет одной командой (нужны установленные Ultralytics/PyTorch):

```powershell
$env:PYTHONPATH = 'D:\PythonLibs'
python tools\model_export\export_pose_release.py `
  --input-dir 'D:\YOLO_Download_Project\models' `
  --output-dir 'D:\Parallel-Finder\release-models'
```

Скрипт экспортирует все `*pose*.pt` в batch-1, 640×640, opset-17 ONNX,
нормализует старое имя `yolo8*` в `yolov8*`, вычисляет SHA-256 и генерирует
`manifest.json`. Создайте Release в
`https://github.com/Pozit1vchic/Parallel-Finder/releases`, затем загрузите
все созданные `.onnx` и `manifest.json` как assets одного релиза. В manifest
имя файла, URL, размер и SHA-256 должны соответствовать asset. Приложение
читает `releases/latest/download/manifest.json`, поэтому новый релиз становится
доступен без обновления exe.

При отсутствии manifest приложение имеет безопасный HTTPS fallback на asset с
тем же именем, но явно помечает его как скачанный без проверки manifest. Для
публичного релиза всегда публикуйте manifest.

### Аудит совместимости

* В каталог UI включены только статические batch-1 файлы. Batch-8/16 требуют
  отдельного профиля PoseEstimator и намеренно не выдаются как совместимые.
* `.pt` и произвольные ONNX без YOLO-pose выхода на 17 keypoints не считаются
  рабочими моделями; ошибка графа показывается до запуска анализа.
* Выбранный провайдер (`dml`, `cuda`, `tensorrt`, `cpu`) передаётся в
  PoseEstimator и проходит preflight; тихого переключения на другой backend
  нет.
* Ключ кэша включает модель, провайдер и optional body-ReID, поэтому смена
  модели не возвращает старые признаки.
* Body-ReID остаётся optional: без OSNet/FastReID ONNX приложение работает в
  pose-only режиме и явно сообщает это в статусе результата.

Проверить установку можно smoke-командой приложения или просто запустить анализ:
ошибка содержит конкретный путь и причину, а неподдерживаемый `.pt` не выдаётся
за рабочую модель.

## Body-ReID (различение людей)

Для отделения похожих поз разных людей приложение дополнительно поддерживает
body-ReID модель. Это не распознавание лица: модель строит вектор внешнего вида
человека по crop тела и сравнивает его с другим crop. Сейчас ожидается ONNX
экспорт OSNet/FastReID с RGB-входом `NCHW` или `NHWC`, batch `1` и выходом
одного embedding-вектора (обычно 256/512 признаков). Входные размеры
`3x256x128` являются стандартом OSNet; фактические статические размеры читаются
из ONNX-графа.

Поддерживаемые автоматические имена:

```
person-reid-osnet.onnx  osnet_x1_0.onnx  osnet.onnx
body-reid.onnx          person-reid.onnx
```

Положить файл можно в `models` рядом с exe, в
`%LocalAppData%\ParallelFinder\models`, в каталог `PF_MODEL_ROOT` или в
исторический `D:\PF_CUDA\models`. Разрешены и другие имена, если в имени есть
`reid` или `osnet`. Переменная `PF_REID_MODEL_PATH` задаёт точный файл.
Если `manifest.json` release-пакета содержит один из этих assets, приложение
может скачать его в `%LocalAppData%\ParallelFinder\models` с проверкой размера и
SHA-256; без записи в manifest сеть не используется.

Если ReID-файл не найден или его граф не совместим, анализ не ломается:
результат помечается как `pose-only`, а фильтр личности отключается. Когда
модель доступна, для каждого `trackId` строится усреднённый L2-нормированный
прототип; совпадение допускается только при cosine similarity не ниже
`0.55`, после чего appearance-сигнал получает 20% итогового score. Порог нужно
калибровать на своих положительных/отрицательных парах, а не трактовать его
как доказательство личности.
