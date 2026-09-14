# Рантаймы провайдеров

Выбор CUDA/TensorRT/DirectML теперь проверяется реальным ONNX Runtime-сеансом.
Если нужных DLL нет, в окне настроек появляется кнопка «Скачать runtime».

Загрузка выполняется только по HTTPS из release-манифеста:

`https://github.com/Pozit1vchic/Parallel-Finder/releases/latest/download/providers.json`

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
отсутствует asset).
