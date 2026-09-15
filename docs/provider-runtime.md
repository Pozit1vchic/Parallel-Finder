# Рантаймы провайдеров

Выбор CUDA/TensorRT/DirectML теперь проверяется реальным ONNX Runtime-сеансом.
Если нужных DLL нет, в окне настроек появляется кнопка «Скачать runtime».

Загрузка выполняется только по HTTPS. Сначала приложение проверяет release-манифест:

`https://github.com/Pozit1vchic/Parallel-Finder/releases/latest/download/providers.json`

Если release ещё не создан, оно пробует тот же файл в ветке `main`:
`https://raw.githubusercontent.com/Pozit1vchic/Parallel-Finder/main/providers/providers.json`.
Для внутренней сборки URL можно переопределить переменной
`PF_PROVIDER_MANIFEST_URL`. В репозитории есть шаблон
[`providers/providers.example.json`](../providers/providers.example.json), но он
намеренно не выдаётся приложению как готовый runtime: архивы и SHA-256 должны
быть реальными.

Пример файла:

```json
{
  "providers": [
    {
      "provider": "cuda",
      "archive": "parallel-finder-runtime-cuda-win64.zip",
      "sizeBytes": 123456789,
      "sha256": "<sha256>",
      "downloadUrl": "https://github.com/Pozit1vchic/Parallel-Finder/releases/download/runtime-v1/parallel-finder-runtime-cuda-win64.zip"
    }
  ]
}
```

Поддерживаемые значения `provider`: `dml`, `cuda`, `tensorrt`, `cpu`.
Архив должен быть ZIP/обычным tar-архивом, который понимает `tar.exe`, и
должен содержать полный side-by-side runtime: `onnxruntime.dll` и совместимые
DLL выбранного Execution Provider. Указывайте размер и SHA-256 — приложение
проверяет их до распаковки.

Файлы распаковываются в `<каталог приложения>/providers/<provider>`. После
успешной установки приложение просит перезапуск: ONNX Runtime нельзя заменить
после того, как его DLL уже загружена текущим процессом. При следующем запуске
папки под `providers/` автоматически добавляются в поиск runtime.

Если release-манифест недоступен, приложение не делает вид, что скачивание
сработало: в настройках показывается причина (нет сети, HTTP-ошибка или
отсутствует asset), а рядом доступна официальная инструкция для выбранного
провайдера. Кнопка «Скачать runtime» становится настоящей установкой только
после публикации архивов и `providers.json` владельцем репозитория.

## Где брать CUDA и TensorRT

Не скачивайте отдельные DLL из случайных архивов. Используйте официальные
страницы NVIDIA и сверяйте версии с таблицей ONNX Runtime:

- [CUDA Toolkit — официальный Download Center NVIDIA](https://developer.nvidia.com/cuda-downloads)
- [TensorRT — официальный Getting Started / Download NVIDIA](https://developer.nvidia.com/tensorrt-getting-started)
- [Требования CUDA Execution Provider в ONNX Runtime](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html)
- [Требования TensorRT Execution Provider в ONNX Runtime](https://onnxruntime.ai/docs/execution-providers/TensorRT-ExecutionProvider.html)

Для CUDA/TensorRT нужны не только Toolkit: должны совпасть NVIDIA-драйвер,
CUDA, cuDNN, TensorRT и сборка ONNX Runtime. На практике для этой сборки
нужно выбрать одну согласованную матрицу версий, а не ставить «самые новые»
компоненты независимо. Поэтому Parallel Finder сначала
делает preflight реальным EP-сеансом и не помечает backend готовым только по
наличию одной DLL. В текущем репозитории архивы провайдеров ещё не опубликованы
в GitHub Release; кнопка загрузки честно сообщает HTTP 404, пока владелец не
создаст `providers.json` и assets релиза.
