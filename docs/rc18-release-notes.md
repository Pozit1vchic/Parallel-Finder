# Parallel Finder 0.1.0-rc.18

## Русский

Обновление выбора главного персонажа и границ сцен. Сохраняет исправления rc.17: ограничение повторов одного шота, статистику выбранного видео, исключение соседних фрагментов одного источника, отдельную папку кэша, один статус анализа и экспорт без потерь.

- Восстановление сложных ракурсов требует нескольких независимых подтверждений лица и проверки тела. Восстановленные треки не расширяют цепочку доказательств личности; их непроверенные края не допускаются в поиск.
- Восстановленные участки ограничиваются подтверждёнными наблюдениями. Конфликт лица и слишком большой промежуток между подтверждениями разрывают участок; непроверенные хвосты не наследуют личность автоматически.
- Старые окна анализа пересчитываются из-за новых правил. Повторный поиск использует обновлённый кэш.
- Добавлены проверки диапазонов личности и диагностика причин отклонения пар. Исправления проверяются на реальных видео из D:\for_tests_pf, включая длинные 4K источники, с CUDA.

Улучшения подтверждены для конкретных проверенных фрагментов, а не только ростом количества карточек. Матчер ещё может пропускать полезные параллели; двойные обнаружения человека и пропущенные склейки могут приводить к ошибкам личности. Общие проценты точности и ускорение первого анализа для rc.18 не заявляются.

**Установка:** запусти Setup или полностью распакуй Portable и открой ParallelFinder.exe. Провайдер меняется в настройках приложения; GPU-компоненты устанавливаются отдельно из приложения. CUDA Toolkit для обычного запуска не нужен. Настройки и скачанные компоненты остаются в общем профиле AppData.

**Качество экспорта:** для сохранения декодированных пикселей, разрешения, глубины цвета и исходного звука используй точный экспорт без потерь MKV/FFV1. Режимы MP4 имеют отдельные компромиссы по перекодированию и ключевым кадрам.

## English

This update improves dominant-person selection and scene boundaries. It retains the rc.17 fixes: repeated-shot limits, statistics for the selected video, exclusion of neighboring fragments from the same source, a dedicated cache folder, a single analysis status, and lossless export.

- Recovering difficult views requires multiple independent face confirmations and corroborating body evidence. Recovered tracks cannot become new identity anchors, and their unverified edges are excluded from matching.
- Recovered intervals are bounded by confirmed observations. A conflicting face or excessive gap between confirmations splits the interval; unverified tails do not automatically inherit an identity.
- Old analysis windows are recomputed for the updated rules. Subsequent searches use the new cache.
- Added regression checks for verified identity ranges and diagnostics explaining pair rejection. Changes are evaluated on the provided real videos in D:\for_tests_pf, including long 4K inputs, with CUDA.

Improvements are supported by specific reviewed examples, not merely a higher result count. Useful parallels may still be missed; duplicate person detections and missed cuts can still cause identity errors. No overall accuracy percentage or faster first-analysis claim is made for rc.18.

**Installation:** run Setup, or fully extract Portable and launch ParallelFinder.exe. Change the provider in application settings; GPU components can be installed separately from the application. CUDA Toolkit is not required for normal use. Settings and downloaded components remain in the shared AppData profile.

**Export quality:** use exact lossless MKV/FFV1 to preserve decoded pixels, resolution, bit depth, and source audio. MP4 modes have separate encoding and keyframe tradeoffs.
