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
файл и имеет более высокий приоритет. После выбора модели в приложении статус
«Модель готова» означает, что файл найден; если его нет, скачивание идёт только
по HTTPS из release-asset с тем же именем.

Проверить установку можно smoke-командой приложения или просто запустить анализ:
ошибка содержит конкретный путь и причину, а неподдерживаемый `.pt` не выдаётся
за рабочую модель.
