# Почему matcher должен сравнивать последовательности

Поиск повторов движения — это не поиск ближайшего стоп-кадра. В научных
работах по skeleton/action matching повторяются три практических идеи:

1. Сначала выравнивать временные последовательности, потому что одинаковое
   движение может идти с разной скоростью и начинаться в разные моменты.
2. Считать совместно позу и temporal dynamics, а не только координаты одного
   кадра.
3. Не принимать решение по среднему score, если внутри последовательности нет
   непрерывного согласованного участка.

В коде Parallel Finder это реализуется как coarse temporal sketch → constrained
DTW → непрерывный temporal run → NMS. Дополнительно введены отдельные ошибки
положения суставов и скорости, обязательная новизна нескольких состояний позы,
motion gate и OSNet appearance gate.

Для дальнейшего обучения/замены rule-based matcher полезны первичные работы:

- [Data Augmented Dynamic Time Warping for Skeletal Action
  Classification](https://www.jstage.jst.go.jp/article/transinf/E101.D/6/E101.D_2017EDP7275/_article/-char/en)
  — DTW для временного выравнивания skeletal sequences.
- [Action Recognition by Joint Spatial-Temporal Motion
  Feature](https://onlinelibrary.wiley.com/doi/10.1155/2013/605469)
  — совместное использование пространственной и temporal информации.
- [Context-Aware Sequence Alignment Using 4D Skeletal
  Augmentation](https://openaccess.thecvf.com/content/CVPR2022/papers/Kwon_Context-Aware_Sequence_Alignment_Using_4D_Skeletal_Augmentation_CVPR_2022_paper.pdf)
  — почему одной попарной frame-схожести недостаточно при различной скорости
  и ракурсе.

Эти ссылки не являются доказательством качества именно этого приложения.
Нужен размеченный набор положительных и отрицательных параллелей, иначе
precision/recall нельзя честно назвать.
