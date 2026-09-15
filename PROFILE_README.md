# Привет, я Арсений 👋

Я строю локальные инструменты, в которых сложная техническая часть остаётся
понятной человеку: от видеоаналитики и моделей до аккуратного интерфейса и
честного статуса «готово / не проверено».

## Сейчас я работаю над

### [Parallel Finder](https://github.com/Pozit1vchic/Parallel-Finder)

Desktop-приложением для поиска повторяющихся движений между видео.

```text
Qt 6 · QML · C++23 · ONNX Runtime · FFmpeg · Windows
```

Внутри проекта:

- временные окна поз вместо сравнения одиночных кадров;
- scene detection и multi-person tracking с `trackId`;
- optional body-ReID для проверки личности между файлами;
- DirectML / CUDA / TensorRT / CPU через проверяемые runtime-провайдеры;
- точные A/B previews, zoom/pan и экспорт монтажных клипов.

Проект находится на стадии рабочего прототипа: core-пайплайн и интерфейс
собраны, а реальные GitHub Releases, GPU-runtime и end-to-end body-ReID ещё
нужно подтвердить на целевых машинах.

## Мой технический фокус

| Направление | Что мне интересно |
| --- | --- |
| C++ и Qt | модульная архитектура, Qt 6, QML, понятные bridge-контракты |
| Computer vision | позы, tracking, temporal matching, ReID и качество данных |
| Инференс | ONNX Runtime, DirectML, CUDA, TensorRT и безопасный fallback |
| Продуктовый UI | layout без обрезаний, readable states, keyboard/accessibility |
| Инженерия | детерминированные результаты, кэширование, диагностика и честные README |

## Как я принимаю решения

1. Сначала фиксирую пользовательскую проблему, а не маскирую симптом.
2. Разделяю «реализовано», «подключено» и «доказано на реальном сценарии».
3. Предпочитаю прозрачный fallback магическому поведению.
4. Делаю интерфейс частью архитектуры: состояние backend, модели и анализа
   должно быть видно пользователю.

## Избранные материалы

- [Полный аудит Parallel Finder](https://github.com/Pozit1vchic/Parallel-Finder/blob/main/docs/full-audit.md)
- [Модели и автоматическая загрузка](https://github.com/Pozit1vchic/Parallel-Finder/blob/main/docs/models.md)
- [GPU-runtime](https://github.com/Pozit1vchic/Parallel-Finder/blob/main/docs/provider-runtime.md)
- [Дизайн-контракт](https://github.com/Pozit1vchic/Parallel-Finder/blob/main/docs/design.md)

## Открыт к хорошим идеям

Особенно интересны задачи на desktop computer vision, C++/Qt, inference
pipelines, инструменты для монтажа и интерфейсы, где качество результата можно
объяснить, измерить и воспроизвести.

<p align="center">
  <sub>Build carefully. Measure honestly. Make difficult tools feel simple.</sub>
</p>
