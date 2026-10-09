# Parallel Finder v0.1.0-rc.20

Изменения относительно RC19.2 / Changes since RC19.2

- Подстройка пары через независимые таймлайны A/B: начало, конец, масштабирование, сдвиг на кадр, актуальные превью и возврат исходного диапазона. Экспорт сохраняет вручную выбранные границы.
- Лист находок PNG: парные ряды A/B, 1/3/5 кадров, таймкоды, сходство и названия исходников; большие списки разбиваются на страницы.
- Избранное, удаление и восстановление пар; Delete и Backspace. Дополнительная настройка включает Ctrl/Shift-мультивыбор и групповые действия.
- Третья вкладка настроек с компактными кнопками и двумя привязками на действие, сохранением сочетаний и проверкой конфликтов.
- Точный экспорт: максимальное/высокое качество, баланс размера либо свой битрейт; настройки учитывают выбранный кодировщик. Быстрый режим сохраняет исходный видеопоток.
- Discord Rich Presence показывает ожидание, анализ, поиск, просмотр результатов и экспорт. Отображением управляют настройки Discord; имена видео и пути не передаются.
- Исправлены автоматический выбор первой пары, состояния просмотра и обводка карточек; добавлены конечные анимации элементов разбора и диалогов.
- Ограничены кеши ONNX-сессий и декодеров; ресурсы runtime освобождаются на ошибках и исключениях. DirectML-вызовы одной сессии защищены от пересечения.
- Новый анализ не сбрасывает результаты во время подстройки/экспорта. Временные превью удаляются при замене результатов, с ограниченными повторными попытками при блокировке файла на Windows.

Реальные превью, PNG и ручной MP4-диапазон проверены на Soldier Boy. Трёхминутный замер простоя после работы: пиковый рост PrivateUsage после стабилизации — 4 КБ, удерживаемых ONNX-сессий — 0. Это не подтверждает причину ранее наблюдавшегося ночного исчерпания памяти и не заменяет многочасовой тест. AMF/QSV аппаратно на этом ПК не проверялись. Подробнее: `docs/pair-review.md`, `docs/memory-audit.md`.

---

- Independent A/B adjustment timelines, frame stepping, live still previews, reset and exact custom export bounds.
- Paginated PNG contact sheets with paired A/B rows, 1/3/5 frames, timestamps and source names.
- Favorites, recoverable removal, Delete/Backspace and optional Ctrl/Shift multi-selection with bulk actions.
- A third settings tab with compact two-slot shortcut bindings, persistence and conflict checks.
- Explicit export quality or custom bitrate for the selected encoder; stream-copy mode preserves the source video bitstream.
- Discord Rich Presence for application activity, without publishing source filenames or paths.
- Correct first-result selection and card borders; finite animations for review controls and dialogs.
- Bounded session/decoder retention, exception-safe runtime cleanup, serialized shared DirectML execution, operation guards and temporary-preview cleanup.

Soldier Boy exercises real previews, PNG and a manually selected MP4 range. A three-minute post-work idle check reports a 4 KiB peak increase after settling and zero retained ONNX sessions; this does not establish the cause of the earlier overnight memory exhaustion. Hardware AMF/QSV execution was not tested on this PC.
