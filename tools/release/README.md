# GitHub Release publisher

Публикация требует токен GitHub с правом `Contents: write`. Токен не хранится
в репозитории и не передаётся через Git remote:

```powershell
powershell -ExecutionPolicy Bypass -File tools/release/publish-github-release.ps1 `
  -Token $env:GITHUB_TOKEN `
  -Tag v0.1.0-models `
  -AssetsDirectory 'D:\Parallel-Finder\release-models'
```

Перед запуском соберите локальные ONNX и проверьте `manifest.json`. Скрипт
создаёт release, загружает `.onnx` и JSON-активы, а при ошибке загрузки удаляет
пустой release. В текущем окружении credentials отсутствуют, поэтому реальная
публикация не выполняется автоматически.
