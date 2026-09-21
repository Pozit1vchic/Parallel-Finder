# Ускорение: инструкция для пользователя и автора сборки

## DirectML — установка из приложения

Настройки → Анализ → Провайдер → DirectML → «Скачать компоненты».
Скачивается около 215 МБ из официального NuGet Microsoft; приложение сверяет SHA-256,
устанавливает только Windows x64 DLL и сохраняет лицензии. Затем полностью закройте
и снова откройте Parallel Finder. Нужны DirectX 12, совместимая видеокарта и её драйвер.
Пакеты: Microsoft.ML.OnnxRuntime.DirectML 1.24.4 и Microsoft.AI.DirectML 1.15.4.
Ни Python, ни CUDA Toolkit для этого пути не нужны. При ошибке загрузки DLL проверьте
также наличие Microsoft Visual C++ Redistributable x64.

## CUDA и TensorRT — куда положить DLL

Это инструкция для **совместимого полного runtime**, не для произвольной смеси DLL.
Одной cudart.dll недостаточно. Нужен установленный драйвер NVIDIA.

Рядом с ParallelFinder.exe создайте:

```text
providers/
  cuda/
    onnxruntime.dll
    onnxruntime_providers_shared.dll
    onnxruntime_providers_cuda.dll
    ... DLL CUDA и cuDNN из совместимого комплекта ...
  tensorrt/
    onnxruntime.dll
    onnxruntime_providers_shared.dll
    onnxruntime_providers_cuda.dll
    onnxruntime_providers_tensorrt.dll
    ... DLL CUDA, cuDNN и TensorRT из совместимого комплекта ...
```

Все DLL одного комплекта кладите **в одну соответствующую папку**, не в System32,
не в корень приложения поверх CPU-runtime. Берите Windows x64 release-версии.
Выберите CUDA или TensorRT в настройках и **перезапустите приложение**.
При следующем запуске выбранная папка будет проверена автоматически.
Внутри уже работающего процесса заменить ONNX Runtime нельзя.

Точные версии и имена зависимостей зависят от выбранной версии ONNX Runtime.
Проверяйте матрицы производителя, а не одинаковость названия «CUDA»:

- [CUDA: совместимость ONNX Runtime, CUDA и cuDNN](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html#requirements).
- [TensorRT: совместимость версий](https://onnxruntime.ai/docs/execution-providers/TensorRT-ExecutionProvider.html#requirements).
- [DirectML и требования к Windows](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html).

Проверки DLL и пробного запуска обязательны: приложение выполняет их само.
Их отключение не ускоряет анализ, но скрывает несовместимость и может привести к падению.
Наличие провайдера не гарантирует ускорение каждой модели.

## Автозагрузка CUDA/TensorRT для пользователей

CUDA/TensorRT assets пока **не опубликованы**. Для их установки одной кнопкой автору
нужно подготовить совместимый комплект выше, проверить на NVIDIA GPU и выполнить
условия лицензий. Затем ZIP с DLL и лицензиями публикуется в релизе `runtime-v1`.
Рядом публикуется `providers.json` по схеме `providers/providers.example.json`:
реальные размер, SHA-256 и URL каждого ZIP. Загрузчик проверяет архив и устанавливает
его в пользовательскую AppData. В обычный Git DLL добавлять не нужно.
Подробности: [runtime-distribution.md](runtime-distribution.md).

## Настройки

Настройки анализа и оформления сохраняются атомарно в небольшом JSON-файле:
`%LOCALAPPDATA%\ParallelFinder\ParallelFinder\settings.json`.
Portable и установленная версия используют этот общий профиль. Папка AppData
обычно скрыта Windows, администратор не нужен. При отсутствии файла язык — English.
Скачанные компоненты лежат рядом в `providers`, модели — в `models`.
