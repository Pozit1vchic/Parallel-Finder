param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [string]$SourceDirectory = (Join-Path $PSScriptRoot '../../models')
)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $SourceDirectory).Path
$stageRoot = (Resolve-Path -LiteralPath $Stage).Path
$filename = 'head-pose-6drepnet360.onnx'
$expectedHash = '8585173bcd7e7d1e5f14baee995cca285814d3f3cf4e1a20d6cafa70787400a3'
$sourceModel = Join-Path $sourceRoot $filename
$sourceLicense = Join-Path $sourceRoot 'head-pose-6drepnet360.LICENSE'
$sourceMetadata = Join-Path $sourceRoot 'head-pose-6drepnet360.json'
if (!(Test-Path -LiteralPath $sourceModel -PathType Leaf) -or
    (Get-Item -LiteralPath $sourceModel).Length -ne 94007930 -or
    (Get-FileHash -LiteralPath $sourceModel -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'Missing or unverified trained head-orientation model; refusing to package a silent fallback'
}
if (!(Test-Path -LiteralPath $sourceLicense -PathType Leaf) -or
    (Get-FileHash -LiteralPath $sourceLicense -Algorithm SHA256).Hash -ne
    '9cbf2690c92d75b75f6c548d96599878cb3692e054b0379d7478db3cd621e8fd') {
    throw 'Missing head-orientation model license'
}
$metadata = Get-Content -LiteralPath $sourceMetadata -Raw | ConvertFrom-Json
if ($metadata.filename -cne $filename -or $metadata.sha256 -cne $expectedHash -or
    $metadata.bytes -ne 94007930 -or $metadata.licenseFile -cne 'head-pose-6drepnet360.LICENSE') {
    throw 'Head-orientation metadata does not match the audited model'
}
$destinationRoot = Join-Path $stageRoot 'models'
New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
foreach ($sourceFile in @($sourceModel,$sourceLicense,$sourceMetadata)) {
    $destination = Join-Path $destinationRoot ([IO.Path]::GetFileName($sourceFile))
    $hash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
            throw "Existing classifier asset differs: $destination"
        }
    } else {
        Copy-Item -LiteralPath $sourceFile -Destination $destination
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
            throw "Classifier copy verification failed: $destination"
        }
    }
}
$actionHash = 'b9316ea2de6a29282fa1567246dc74670052525319e48cbab02b6b91ff10c481'
$actionModel = Join-Path $sourceRoot 'ava-slowfast-r50.onnx'
$actionLicense = Join-Path $sourceRoot 'ava-slowfast-r50.LICENSE'
$actionMetadata = Join-Path $sourceRoot 'ava-slowfast-r50.json'
if (!(Test-Path -LiteralPath $actionModel -PathType Leaf) -or
    (Get-Item -LiteralPath $actionModel).Length -ne 135286691 -or
    (Get-FileHash -LiteralPath $actionModel -Algorithm SHA256).Hash -ne $actionHash) {
    throw 'Missing or unverified trained video-action model'
}
if (!(Test-Path -LiteralPath $actionLicense -PathType Leaf) -or
    (Get-FileHash -LiteralPath $actionLicense -Algorithm SHA256).Hash -ne
    'eb2e7c520ac305e46ca8345c4efa1f77669a77779fd6f53acd3a9616f2144518') {
    throw 'Missing video-action model license'
}
$actionInfo = Get-Content -LiteralPath $actionMetadata -Raw | ConvertFrom-Json
if ($actionInfo.filename -cne 'ava-slowfast-r50.onnx' -or $actionInfo.sha256 -cne $actionHash -or
    $actionInfo.bytes -ne 135286691 -or $actionInfo.licenseFile -cne 'ava-slowfast-r50.LICENSE' -or
    $actionInfo.applicationClassificationIntegration -ne $true) {
    throw 'Video-action metadata does not match the integrated classifier asset'
}
foreach ($sourceFile in @($actionModel,$actionLicense,$actionMetadata)) {
    $destination = Join-Path $destinationRoot ([IO.Path]::GetFileName($sourceFile))
    $hash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
            throw "Existing classifier asset differs: $destination"
        }
    } else {
        Copy-Item -LiteralPath $sourceFile -Destination $destination
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
            throw "Classifier copy verification failed: $destination"
        }
    }
}
Write-Host 'Staged verified head-orientation and video-action models, metadata and licenses'
