# Model assets

Веса моделей **не хранятся в репозитории**. Приложение использует ONNX-экспорт
YOLO-pose, а не исходные Ultralytics `.pt`. При выборе отсутствующей модели UI
показывает `↓ скачать`, загружает её из последнего GitHub Release и проверяет
размер/SHA-256 из `manifest.json` до атомарной установки.

Установленные файлы хранятся автоматически в:

```text
%LocalAppData%\ParallelFinder\models
```

### Подготовка release из D:\YOLO_Download_Project\models

```powershell
$env:PYTHONPATH = 'D:\PythonLibs'
python tools\model_export\export_pose_release.py `
  --input-dir 'D:\YOLO_Download_Project\models' `
  --output-dir 'D:\Parallel-Finder\release-models'
```

Скрипт экспортирует каждый `*pose*.pt` в статический batch-1 ONNX 640×640 и
создаёт рядом `manifest.json`. Создайте Release в
`Pozit1vchic/Parallel-Finder` и загрузите **все** `.onnx` плюс `manifest.json`
как assets одного релиза. Имена должны совпадать с manifest: приложение берёт
их по адресу `releases/latest/download/<filename>`.

Для разработки можно задать `PF_MODEL_ROOT` на папку с ONNX-файлами, а
`PF_MODEL_PATH` — на конкретный файл. Ни `.pt`, ни `manifest.json` не нужно
класть в установочную папку вручную.

Эта папка игнорируется Git за исключением данного README.
