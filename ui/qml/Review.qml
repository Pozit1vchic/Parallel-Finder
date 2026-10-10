pragma Singleton
import QtQuick
import PfUi
import PfUiBridge

QtObject {
    id: root
    property bool multiSelect: false
    property var bindings: ({})
    readonly property var actions: [
        {id: "previous", ru: "Предыдущая пара", en: "Previous pair", keys: ["Up"]},
        {id: "next", ru: "Следующая пара", en: "Next pair", keys: ["Down"]},
        {id: "hide", ru: "Удалить / восстановить пару", en: "Remove / restore pair", keys: ["Del", "Backspace"]},
        {id: "favorite", ru: "Добавить / убрать избранное", en: "Toggle favorite", keys: ["F"]},
        {id: "mark", ru: "Отметить для экспорта", en: "Mark for export", keys: ["X"]},
        {id: "play", ru: "Воспроизвести / приостановить", en: "Play / pause", keys: ["Space"]},
        {id: "adjust", ru: "Подстроить пару", en: "Adjust pair", keys: ["A"]},
        {id: "export", ru: "Экспорт отмеченных", en: "Export marked pairs", keys: ["Ctrl+E"]},
        {id: "sheet", ru: "Лист находок", en: "Contact sheet", keys: ["Ctrl+Shift+E"]},
        {id: "all", ru: "Выделить все пары", en: "Select all pairs", keys: ["Ctrl+A"]},
        {id: "clear", ru: "Снять выделение", en: "Clear selection", keys: ["Ctrl+Shift+A"]},
        {id: "color0", ru: "Убрать цвет", en: "Clear color", keys: ["0"]},
        {id: "color1", ru: "Красный цвет", en: "Red color", keys: ["1"]},
        {id: "color2", ru: "Синий цвет", en: "Blue color", keys: ["2"]},
        {id: "color3", ru: "Зелёный цвет", en: "Green color", keys: ["3"]},
        {id: "color4", ru: "Фиолетовый цвет", en: "Purple color", keys: ["4"]},
        {id: "color5", ru: "Золотой цвет", en: "Gold color", keys: ["5"]},
        {id: "undo", ru: "Отменить последнее действие", en: "Undo last action", keys: ["Ctrl+Z"]},
        {id: "nextUnreviewed", ru: "Следующая неразобранная пара", en: "Next unreviewed pair", keys: ["N"]},
        {id: "reviewed", ru: "Разобрано / не разобрано", en: "Toggle reviewed", keys: ["R"]}
    ]
    readonly property var words: ({
        undo: ["Отменить", "Undo"], nextUnreviewed: ["Следующая неразобранная", "Next unreviewed"],
        reviewed: ["Разобрано", "Reviewed"], unreviewed: ["Не разобрано", "Unreviewed"],
        reviewedHint: ["Отметить как разобранную; цвет и избранное не меняются", "Mark as reviewed; color and favorite stay unchanged"],
        allReviewed: ["Все показанные пары разобраны", "All visible pairs are reviewed"],
        sourceSearch: ["Поиск по имени сценпака…", "Search scene pack name…"],
        allSources: ["Все сценпаки", "All scene packs"], sourceHint: ["Ищем по обеим сторонам A/B. «Выбрать все» выделяет только показанные пары.", "Search covers both A/B sources. Select all marks only visible pairs."],
        hotkeys: ["Горячие клавиши", "Keyboard shortcuts"],
        keysHint: ["Выбери сочетание и нажми новые клавиши. Esc отменяет ввод.", "Click a binding and press new keys. Esc cancels."],
        multi: ["Включить мультивыбор", "Enable multiple selection"],
        multiHint: ["Ctrl — отдельные пары, Shift — диапазон. Цвет, избранное и удаление применяются к выделению.", "Ctrl selects individual pairs; Shift selects a range. Color, favorite and removal apply to the selection."],
        press: ["Нажми клавиши…", "Press keys…"], unset: ["Не назначено", "Unassigned"],
        conflict: ["Сочетание уже используется: ", "Binding already used: "], reset: ["Сбросить клавиши", "Reset shortcuts"],
        all: ["Все", "All"], favorites: ["Избранное", "Favorites"], hidden: ["Удалённые", "Removed"],
        remove: ["Удалить пару", "Remove pair"], restore: ["Восстановить пару", "Restore pair"],
        favorite: ["Избранное", "Favorite"], adjust: ["Подстроить пару", "Adjust pair"],
        sheet: ["Лист находок", "Contact sheet"], sheetHint: ["Парные ряды кадров с таймкодами. Большой список делится на изображения по 4 пары.", "Paired frame rows with timestamps. Large lists are split into images of 4 pairs."],
        scope: ["Пары для листа", "Pairs to include"], visible: ["Все показанные пары", "All visible pairs"], marked: ["Только отмеченные", "Marked pairs only"],
        frames: ["Кадров на каждой стороне", "Frames on each side"], folder: ["Папка для изображений", "Image folder"],
        choose: ["Выбрать папку", "Choose folder"], save: ["Сохранить PNG", "Save PNG"],
        start: ["Начало, сек", "Start, sec"], end: ["Конец, сек", "End, sec"],
        left: ["Левая сторона", "Left side"], right: ["Правая сторона", "Right side"],
        apply: ["Применить", "Apply"], original: ["Вернуть исходную пару", "Restore original pair"],
        rangeHint: ["Перетаскивай границы на таймлайне. ±1 кадр сдвигает фрагмент; экспорт использует выбранные границы.", "Drag the timeline boundaries. ±1 frame shifts the segment; export uses the selected boundaries."],
        invalid: ["Проверь границы: начало должно быть меньше конца и находиться внутри видео.", "Check the range: start must precede end, within the video."],
        selected: ["Выделено", "Selected"], empty: ["В этом списке пока нет пар", "No pairs in this view"],
        preview: ["Просмотреть фрагмент", "Preview segment"], close: ["Закрыть", "Close"],
        clearKey: ["Убрать назначение", "Clear binding"], saving: ["Сохраняем изображения…", "Saving images…"]
    })
    function t(key) { const value = words[key]; return value ? value[L10n.language === "ru" ? 0 : 1] : key }
    function label(action) { return L10n.language === "ru" ? action.ru : action.en }
    function keys(id) { return bindings[id] || [] }
    function persist() {
        const prefs = Object.assign({}, AppInfo.loadPreferences())
        prefs.review = {multiSelect: multiSelect, bindings: bindings}
        AppInfo.savePreferences(prefs)
    }
    function setMulti(value) { multiSelect = value; persist() }
    function assign(id, slot, sequence) {
        for (const action of actions) for (let i = 0; i < keys(action.id).length; ++i)
            if (sequence && keys(action.id)[i].toLowerCase() === sequence.toLowerCase()
                    && (action.id !== id || i !== slot)) return t("conflict") + label(action)
        const next = Object.assign({}, bindings), values = keys(id).slice()
        while (values.length <= slot) values.push("")
        values[slot] = sequence; next[id] = values; bindings = next; persist(); return ""
    }
    function reset() {
        const next = {}; for (const action of actions) next[action.id] = action.keys.slice()
        bindings = next; persist()
    }
    Component.onCompleted: {
        const saved = AppInfo.loadPreferences().review || {}, next = {}, used = {}
        multiSelect = saved.multiSelect === true
        for (const action of actions) {
            const raw = saved.bindings && Array.isArray(saved.bindings[action.id]) ? saved.bindings[action.id] : action.keys
            next[action.id] = raw.slice(0, 2).map(function(key) {
                if (typeof key !== "string" || key.length > 80 || (key && used[key.toLowerCase()])) return ""
                if (key) used[key.toLowerCase()] = true
                return key
            })
        }
        bindings = next
    }
}
