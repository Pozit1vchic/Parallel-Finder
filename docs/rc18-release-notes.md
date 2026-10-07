# Parallel Finder 0.1.0-rc.18

## Русский

[+] Ускорен матчер на длинных наборах из нескольких источников: проверка копий кадров использует точный индекс консервативных границ, повторяющиеся ракурсы проверяются по необходимости, одинаковые запросы личности переиспользуют результат обхода индекса.
[+] Сохранены проверки человека, позы, временной согласованности и копий исходного видео; пороги принятия не снижены ради скорости.
[+] Сравнения используют доступные потоки CPU, до 32; ручная настройка потоков применяется и к поиску.
[+] После 85% отображаются отдельные этапы поиска: исключение копий, поиск кандидатов, сравнение движений, отбор уникальных пар и подготовка результатов; поиск поддерживает отмену.
[+] Добавлены именованные цветовые группы результатов, массовое назначение, удаление меток, фильтрация и экспорт только выбранных пар видимой группы.
[+] Метки сохраняются для конкретной пары и файлов между запусками; замена видео по тому же пути не наследует старые метки.
[+] Добавлен фильтр классификации: положение головы/тела, поворот, в том числе головы, шаг, подъём, опускание и другие уже измеряемые движения с направлением. Неоднозначные движения отображаются как смешанные.
[+] Источники, не вошедшие в подтверждённую группу одного человека, отмечаются в списке с пояснением.
[+] Согласованы плавные переходы диалогов и размытия фона; настройка уменьшения анимаций сохраняется.
[-] Исправлена прокрутка списка Sources: колесо над списком больше не перемещает панель настроек, в том числе на границах списка.
[-] Исправлена сборка с чистого build: зависимости готовятся перед изолированными тестами запуска.
[-] В пакет включается только отслеживаемая документация; промежуточные staging-папки успешной сборки автоматически удаляются. Дополнены правила игнорирования видео, профилей производительности и временных обновлений.

Контрольный A/B этапа поиска, на одном потоке: 5 источников / 300 окон — 290 390 мс → 4 839 мс, все 149 пар совпали полностью; 100 окон — 32 042 мс → 554 мс, все 49 пар совпали. Это нагрузочная проверка на записанной геометрии поз с синтетическими независимыми изображениями; она исключает декодирование и модели и не измеряет смысловую точность.

Реальный CUDA-прогон пяти разных 40-секундных 4K-фрагментов Эллиота: 4 818 кадров, около 56 с холодного анализа; поиск около 2,3 с. Повторный запуск с кэшем около 0,3 с. Допущены 3 из 5 источников: источник без достаточных подтверждений лица и источник с противоречащей личностью исключены. Эта проверка не обещает включение всех видео без доказательств личности и не заменяет длительный прогон всех шести часов исходного материала.

GPU используется моделями при выбранном доступном провайдере; сам поиск поз выполняется на CPU. Для этих 10-битных HEVC-фрагментов декодер сохранил проверенный CPU-путь. Память и видеокарта не загружаются искусственно до 100%.

## English

[+] Faster matching on long multi-source sets: copied footage uses an exact conservative range index, recurring views are checked on demand, and identical identity queries reuse their raw index traversal.
[+] Preserved person, pose, temporal and source-footage checks; acceptance thresholds were not lowered for speed.
[+] Exact comparisons use available CPU workers, up to 32; the manual thread setting also applies to matching.
[+] Progress after 85% reports copied-footage checks, candidate retrieval, movement comparisons, unique-pair selection and result preparation; matching supports cancellation.
[+] Added named color groups, bulk tagging, tag removal, filtering and export restricted to selected pairs in the visible group.
[+] Tags persist for an exact pair and source files across runs; replacing a video does not inherit its previous tags.
[+] Added classification filtering for head/body position, turns including head turns, steps, raises, lowers and other measured gestures with direction. Ambiguous movements remain marked as mixed.
[+] Sources excluded from the verified single-person group are marked in the source list with an explanation.
[+] Unified dialog and backdrop transitions while respecting reduced motion.
[-] Fixed Sources wheel scrolling so it does not move the settings rail, including at list boundaries.
[-] Fixed clean-build startup verification by deploying dependencies before isolated launch tests.
[-] Packages include only tracked documentation; successful packaging removes its staging intermediates. Updated ignore rules for videos, profiling captures and temporary updates.

Controlled matcher-only A/B, one worker: 5 sources / 300 windows — 290,390 ms → 4,839 ms with all 149 pairs identical; 100 windows — 32,042 ms → 554 ms with all 49 pairs identical. The workload uses recorded pose geometry and synthetic independent images; it excludes decoding/inference and does not measure semantic precision.

A real CUDA run of five different 40-second 4K Elliot excerpts processed 4,818 frames in about 56 seconds cold, with about 2.3 seconds in matching; a cached run took about 0.3 seconds. Three sources were admitted; missing face evidence and conflicting identity excluded the other two. This is not a guarantee that every source is accepted or a full six-hour end-to-end benchmark.

Models use the selected available GPU provider; pose search runs on CPU. These 10-bit HEVC excerpts retained the validated CPU decoding path. RAM and GPU utilization are not artificially driven to 100%.
