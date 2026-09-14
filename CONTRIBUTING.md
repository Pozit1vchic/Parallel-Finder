# Contributing to Parallel Finder

Спасибо за вклад. Проект разделяет вычислительное ядро, GPU-инференс, сервисы
и QML, поэтому небольшие изолированные изменения проще проверять и принимать.

## Перед pull request

1. Выберите подходящий слой: `core`, `gpu`, `services`, `exporters` или `ui`.
2. Не добавляйте `.pt`/`.onnx`, личные settings и build-артефакты в Git.
3. Для исправления поведения добавьте regression-тест в `tests/`.
4. Проверьте Debug и Release, если изменение затрагивает CMake или QML.

```bash
cmake --preset ucrt64-debug
cmake --build --preset ucrt64-debug
ctest --preset ucrt64-debug --output-on-failure

cmake --preset ucrt64-release
cmake --build --preset ucrt64-release
ctest --preset ucrt64-release --output-on-failure
```

## Правила изменений

- C++ — C++23, RAII, явное владение и предупреждения без новых suppressions.
- `core/` и `exporters/` не импортируют Qt.
- Видимый текст QML проходит через `L10n.qml`.
- Цвета, радиусы и размеры QML берутся из `Theme.qml`.
- Не добавляйте декоративный motion, если он не сообщает пользователю о смене
  состояния.
- Если функция недоступна без конкретного runtime, модели или DLL, UI должен
  сказать об этом явно, а не показывать фиктивную готовность.

## Модели

Модели готовятся отдельно через `tools/model_export/` и публикуются assets
GitHub Release вместе с `manifest.json`. См. [`docs/models.md`](docs/models.md).
