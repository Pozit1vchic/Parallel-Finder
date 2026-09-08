# Parallel-Finder

Поиск параллельных движений в видео: находит пары клипов/моментов `A[t0,t1] ↔ B[t2,t3]`,
где один и тот же персонаж выполняет визуально то же движение. All-pairs сравнение
окон движения (HNSW-префильтр → DTW с полосой Сакое-Чиба), GPU-инференс через
ONNX Runtime.

## Лицензия

- **Код проекта — [AGPL-3.0](LICENSE).**
- **Веса моделей (.pt/.onnx) не коммитятся в репозиторий** — весов YOLO-pose
  (Ultralytics, AGPL-3.0) нет в дереве исходников; они скачиваются отдельным
  fetch-механизмом при первом запуске / берутся из GitHub Releases.
- **FFmpeg**: используется сборка MSYS2 UCRT64 (ffmpeg 9.x), конфигурация
  содержит `--enable-gpl --enable-version3` — это **GPLv3-сборка** (x264/x265/xvid).
  Это зафиксировано в релизе и в About-окне.
- Прочие сторонние компоненты: Qt6 (LGPLv3, динамическая линковка), ONNX Runtime
  (MIT), Google Test (BSD-3).

## Технологический стек

| Компонент | Выбор |
|---|---|
| Язык | C / C++23 |
| UI | Qt6 QML + C++ bridge |
| Сборка | CMake ≥ 3.25 + Ninja, MSYS2 UCRT64 (GCC) |
| GPU-инференс | ONNX Runtime, готовые прекомпилированные EP: TensorRT → CUDA → DirectML → CPU (fallback-цепочка, автоопределение + ручной выбор `auto/cuda/dml/cpu`) |
| Самописные CUDA-ядра | **Не в v1** (nvcc не поддерживает GCC/MinGW host-компилятор) |
| Релиз | Портативная папка (exe + DLL + models/), без автообновлений в v1 |

## Структура (модули = CMake-таргеты, ядро без Qt)

```
app/            ParallelFinder (thin: init → Splash → Main)
gpu/    pfgpu        getDeviceInfo, ORT-сессии (модель|провайдер), auto/cuda/dml/cpu
core/   pfcore       VideoDecoder, SceneDetector, PoseEstimator, DominantPerson, MotionMatcher, JobManager, PFCACHE1
services/ pfservices ThumbnailCache LRU64, CutService (ffmpeg QProcess), ThemeStore, settings.json
exporters/ pfexporters JSON/CSV/TXT/EDL/FCPXML/AEP(.jsx + json)
ui/     pfui         QML Splash/Main/Settings/DemoMode + C++ bridge
tests/  gtest + qtest
models/ *.onnx (gitignore) — не коммитятся
```

`pfcore` и `pfexporters` не содержат Qt (guard-таргет `pfcore_qt_ban` проверяет это
на каждой сборке). Новый GPU-провайдер добавляется через абстрактную фабрику в
`pfgpu`, без правок `pfcore`. ORT загружается версионно-агностично:
`LoadLibraryW("onnxruntime.dll")` + `OrtGetApiBase()`, линковка не требуется.

## Сборка (MSYS2 UCRT64)

```bash
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-qt6-base \
          mingw-w64-ucrt-x86_64-qt6-declarative mingw-w64-ucrt-x86_64-qt6-shadertools \
          mingw-w64-ucrt-x86_64-gtest mingw-w64-ucrt-x86_64-ffmpeg \
          mingw-w64-ucrt-x86_64-onnxruntime

cmake --preset ucrt64-debug
cmake --build --preset ucrt64-debug
ctest --preset ucrt64-debug
```

Тесты автоматически используют `QT_QPA_PLATFORM=offscreen` там, где нужно окно.

## Тяжёлые GPU-зависимости

ORT-GPU / CUDA / TensorRT / DML-redist при сборке размещаются **только в
`D:\PF_CUDA`** — отдельно от репозитория и от диска C. В репозиторий не попадают.

## Стадии

0. ✅ Скелет: CMake-пресеты, все таргеты, ctest зелёный, CI
1. pfgpu: getDeviceInfo, кэш сессий, fallback-цепочка, бейдж
2. VideoDecoder (DISPLAYMATRIX/SAR/VFR/RAII) · 2b. SceneDetector
3. Модель+инференс · треки+матчер (all-pairs, HNSW+DTW) · JobManager · PFCACHE1
4. Экспорт + сервисы (CutService, ThumbnailCache, settings.json)
5. UI (Splash/Main/Settings/DemoMode) · 5b. Qt-деплой
6. GPU-бандл + перф-валидация + финальный аудит
