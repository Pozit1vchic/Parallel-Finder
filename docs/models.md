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

* Pose-модели из Release публикуются в универсальном batch-1 профиле. Batch-8
  остаётся developer/GPU-профилем и не показывается в селекторе, пока не будет
  добавлен в manifest как отдельный проверенный asset.
* `.pt` и произвольные ONNX без YOLO-pose выхода на 17 keypoints не считаются
  рабочими моделями; ошибка графа показывается до запуска анализа.
* Выбранный провайдер (`dml`, `cuda`, `tensorrt`, `cpu`) передаётся в
  PoseEstimator и проходит preflight; тихого переключения на другой backend
  нет.
* Ключ кэша включает fingerprint pose/ReID-моделей, провайдер, режим, настройки
  scene detector и fingerprint видео. Поэтому замена ONNX по тому же пути или
  смена segmentation-параметров не возвращает старые признаки.
* Body-ReID необходим для публикации межперсонажных совпадений. Без OSNet/
  FastReID ONNX приложение безопасно завершает анализ без таких совпадений и
  явно сообщает причину в статусе, вместо того чтобы выдавать ложные пары.

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

Проверенный upstream для подготовки такого asset — [Kaiyang Zhou /
deep-person-reid](https://github.com/KaiyangZhou/deep-person-reid) и его
[каталог OSNet-весов на Hugging Face](https://huggingface.co/kaiyangzhou/osnet).
Они поставляют исходные PyTorch-веса; перед публикацией в manifest их нужно
экспортировать в batch-1 ONNX и проверить вход `3x256x128` и embedding output.
В текущей ревизии такой asset подготовлен локально как
`D:\PF_CUDA\models\person-reid-osnet.onnx` (8-bit OSNet x0.25, batch-1,
512-мерный embedding, upstream-модель помечена MIT) и не добавлен в Git из-за
размера. Его можно
переместить в каталог рядом с exe или в `%LocalAppData%\ParallelFinder\models`;
без него приложение честно переключается в `pose-only`.

Положить файл можно в `models` рядом с exe, в
`%LocalAppData%\ParallelFinder\models`, в каталог `PF_MODEL_ROOT` или в
исторический `D:\PF_CUDA\models` или `D:\YOLO_Download_Project\models`.
Разрешены и другие имена, если в имени есть
`reid` или `osnet`. Переменная `PF_REID_MODEL_PATH` задаёт точный файл.
Если `manifest.json` release-пакета содержит один из этих assets, приложение
может скачать его в `%LocalAppData%\ParallelFinder\models` с проверкой размера и
SHA-256; без записи в manifest сеть не используется.

Если ReID-файл не найден или его граф не совместим, анализ не ломается, но
межперсонажные результаты блокируются (`ReID: модель не найдена`). Когда
модель доступна, для каждого `trackId` и сцены строится усреднённый
L2-нормированный прототип; UI допускает identity-gate при cosine similarity
не ниже `0.76` (консервативный core по умолчанию — `0.80`). Appearance не
увеличивает итоговый процент: это только фильтр личности, а не «фантомные»
баллы за похожую внешность.
В пределах одного исходного файла разные `trackId` считаются разными людьми и
не объединяются даже при высоком cosine: это защищает от ложного подтверждения
двух актёров с похожей позой. Это identity-gate против похожих поз разных
людей, а не доказательство личности: порог всё равно нужно калибровать на
своих положительных и отрицательных парах.
