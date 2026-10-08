# Parallel Finder v0.1.0-rc.19

Изменения относительно RC18.8 / Changes since RC18.8

[+] Распараллелены поиск кандидатов и выбор основного персонажа; повторные проходы матчера используют уже выполненные расчёты.
[+] Добавлено декодирование поддерживаемого 10-битного HEVC через NVIDIA и параллельное чтение ракурсов сцен.
[+] Улучшено сравнение наблюдаемых поз тела при смене ракурса; убран скрытый 12-секундный запрет между отдельными сценами.
[-] Исправлены повторяющиеся статичные пары из одного ракурса и выдача крупного плана другого персонажа как движения тела.
[-] Ограничены временные кеши и размеры входных окон; служебные данные освобождаются раньше, усилена проверка повреждённых файлов кеша.

Ограничение: короткие появления и позы со скрытыми конечностями всё ещё могут пропускаться; полнота поиска всех параллелей не подтверждена.

---

[+] Parallelized candidate retrieval and identity selection; repeated matcher passes reuse completed calculations.
[+] Added NVIDIA decoding for supported 10-bit HEVC and parallel sampling of scene views.
[+] Improved comparison of observed body poses across viewpoints; removed the hidden 12-second floor between separate scenes.
[-] Fixed repetitive static pairs from the same camera view and cropped faces of another character being accepted as body motion.
[-] Bounded temporary caches and input-window sizes; release scratch data earlier and validate damaged cache files more strictly.

Limitation: brief appearances and poses with occluded limbs can still be missed; exhaustive recall of all parallels has not been established.
