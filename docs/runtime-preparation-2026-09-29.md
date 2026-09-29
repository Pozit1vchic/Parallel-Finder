# Повторная подготовка runtime — 29 сентября 2026

Новый входной комплект: `release/runtime-input-final-20260929`.
Новый выходной комплект: `release/runtime-final-20260929`.
Предыдущий `runtime-assets-20260929-parts` не использовать для новой публикации:
там отсутствуют документы, а его SHA-256 не соответствует новым архивам.

## Происхождение документов

- CUDA EULA и дополнительные notices скопированы из установленного
  `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8/EULA.txt` и `LICENSE`.
  Версия упакованного `cudart64_12.dll`: `6,14,11,12080`.
- cuDNN LICENSE — из исходного дистрибутива 9.10.0.56 для CUDA 12.
- TensorRT LICENSE извлечена без редактирования из
  `tensorrt-10.9.0.34.dist-info/LICENSE.txt` внутри wheel исходного комплекта
  `D:/CUDA_Libraries/TensorRT-10.9.0.34/python`; добавлен его `doc/Acknowledgements.txt`.
- ONNX Runtime LICENSE и ThirdPartyNotices загружены из официального репозитория
  Microsoft, ревизия `2d92497`, соответствующая FileVersion упакованных DLL:
  `https://raw.githubusercontent.com/microsoft/onnxruntime/2d92497/LICENSE`
  и `https://raw.githubusercontent.com/microsoft/onnxruntime/2d92497/ThirdPartyNotices.txt`.
- Документы DirectML сохранены из установленного комплекта.

## Ограничения проверки

Тест `PoseEstimator.RunsReleaseAssetWhenRequested` поддерживает
`PF_TEST_MODEL_PROVIDER=cuda` или `tensorrt`, по умолчанию использует CPU.
Для CUDA/TensorRT используется реальная YOLO11n ONNX и синтетический кадр 640×640;
это тест выполнения графа, не точности детекции и не полного анализа видео.
Тесту необходимы служебные DLL среды сборки; CUDA Toolkit исключён из PATH,
ORT указан явно из подготовленного комплекта. Это не чистая Windows-машина.

Результат YOLO11n: CUDA — exit 0, 1,20 с; TensorRT — exit 0, 85,60 с с первой
подготовкой движка. Это время всего тестового процесса, не сравнение throughput.
Машиночитаемый отчёт: `inference-validation.json` во входном комплекте.

Полный CTest: 5/5, 23,77 с. Отдельный тест готовых архивов
`ProviderStore.PreparedReleasePayloadsVerifyAndReassemble`: PASS, 57,21 с;
проверены SHA-256 всех пакетов, сборка полного TensorRT из частей и чтение ZIP.

Наличие документов не является юридическим заключением о праве публикации.
Раздел 12.1 лицензии TensorRT отдельно описывает распространение libnvinfer и
libnvinfer_plugin; комплект также содержит parser, builder resource и другие DLL.
Право распространения полного состава и совместимость с AGPL проекта требуют
отдельного подтверждения владельцем проекта. Не менять `redistributionReviewCompleted`
на `true` только потому, что все файлы LICENSE теперь присутствуют.

## Файлы для релиза runtime-v1 после проверки условий распространения

Из новой выходной папки:

1. `parallel-finder-runtime-cuda-win64.zip`
2. `parallel-finder-runtime-dml-win64.zip`
3. `parallel-finder-runtime-tensorrt-win64.zip.part001`
4. `parallel-finder-runtime-tensorrt-win64.zip.part002`
5. `providers.json` — последним, после всех бинарных файлов.

Не загружать полный TensorRT ZIP, `providers.draft.json`, validation-отчёты,
кэш движков или логи. `providers.json` — точная копия подготовленного каталога,
его наличие само по себе не обозначает завершённую проверку прав распространения.
Старый манифест не подходит к этим архивам. После публикации нужен сквозной тест
скачивания на машине друга и новая сборка приложения с поддержкой `parts`.
