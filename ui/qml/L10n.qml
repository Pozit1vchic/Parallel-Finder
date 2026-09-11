// Localization dictionary (spec section 5: строки только через словарь).
// Stage 0: minimal seed set; full UI strings arrive with stage 5.
pragma Singleton
import QtQuick

QtObject {
    readonly property string language: "ru"

    readonly property var _dict: ({
        ru: {
            "app.title": "Parallel Finder",
            "app.stage": "стадия 1 — GPU и провайдеры",
            "splash.loading": "Инициализация…",
            "top.gpu": "GPU: %1",
            "top.gpu.tooltip": "Провайдер вывода: %1 · ONNX Runtime: %2",
            "panel.sources": "Источники",
            "panel.sliders": "Параметры поиска",
            "panel.results": "Результаты",
            "center.preview": "Превью",
            "center.empty": "Загрузите видео, чтобы начать",
            "status.ready": "Готово",
        },
        en: {
            "app.title": "Parallel Finder",
            "app.stage": "stage 1 — GPU & providers",
            "splash.loading": "Initializing…",
            "top.gpu": "GPU: %1",
            "top.gpu.tooltip": "Inference provider: %1 · ONNX Runtime: %2",
            "panel.sources": "Sources",
            "panel.sliders": "Search parameters",
            "panel.results": "Results",
            "center.preview": "Preview",
            "center.empty": "Load a video to begin",
            "status.ready": "Ready",
        }
    })

    function t(key) {
        var lang = _dict[language] || _dict["en"];
        return (key in lang) ? lang[key] : key;
    }
}
