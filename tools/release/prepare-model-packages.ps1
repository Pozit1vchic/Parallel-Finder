#requires -Version 7.0
[CmdletBinding()]
param(
    [string] $SourceDirectory = 'D:\YOLO_Download_Project\models',
    [Parameter(Mandatory)][string] $OutputDirectory,
    [string] $Repository = 'Pozit1vchic/Parallel-Finder',
    [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+-models$')][string] $Tag = 'v0.1.0-models'
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceDirectory).Path
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'Use a new output directory; existing packages are never overwritten.' }
$names = foreach ($family in @('yolov8', 'yolo11', 'yolo26')) {
    foreach ($size in @('n', 's', 'm', 'l', 'x')) { "$family$size-pose.onnx" }
}
foreach ($name in $names) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $name) -PathType Leaf)) { throw "Missing model: $name" }
}
New-Item -ItemType Directory -Path $destination | Out-Null
$models = foreach ($name in $names) {
    $original = Join-Path $source $name
    $target = Join-Path $destination $name
    Copy-Item -LiteralPath $original -Destination $target
    $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash.ToLowerInvariant()) { throw "Copy verification failed: $name" }
    [ordered]@{
        filename = $name
        url = "https://github.com/$Repository/releases/download/$Tag/$name"
        sizeBytes = (Get-Item -LiteralPath $target).Length
        sha256 = $hash
        license = 'AGPL-3.0'
        minimumAppVersion = '0.1.0'
        input = @(1, 3, 640, 640)
    }
}
[ordered]@{ schemaVersion = 1; models = @($models) } | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8NoBOM
Write-Host "Prepared $($models.Count) models in $destination. Run inference validation before publication."
