[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v?[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.-]+)?$')]
    [string] $Tag,

    [string] $Repository = 'Pozit1vchic/Parallel-Finder',

    [string] $AssetsDirectory = 'D:\Parallel-Finder\release-models',

    [switch] $Prerelease,

    [switch] $SkipRemoteVerification,

    [switch] $ValidateOnly
)

$ErrorActionPreference = 'Stop'

function Require-Command([string] $Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "'$Name' is required. Install GitHub CLI from https://cli.github.com/, then run 'gh auth login'."
    }
}

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$assetRoot = [System.IO.Path]::GetFullPath($AssetsDirectory)
$manifestPath = Join-Path $assetRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "manifest.json was not found in '$assetRoot'. Run tools/model_export/export_pose_release.py first."
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (-not $manifest.models -or @($manifest.models).Count -eq 0) {
    throw 'manifest.json has no models array.'
}

$uploadPaths = [System.Collections.Generic.List[string]]::new()
$filenames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($asset in @($manifest.models)) {
    if ($asset.filename -notmatch '^[A-Za-z0-9_-]+\.onnx$') {
        throw 'Unsafe model filename.'
    }
    if (-not $filenames.Add($asset.filename)) { throw "Duplicate model filename: $($asset.filename)" }
    $expectedUrl = "https://github.com/$Repository/releases/download/$Tag/$($asset.filename)"
    if ($asset.url -cne $expectedUrl) { throw "Model URL must target this release: $expectedUrl" }
    if (-not $asset.filename -or -not $asset.sha256 -or -not $asset.sizeBytes) {
        throw 'Every manifest model needs filename, sizeBytes and sha256.'
    }
    $path = Join-Path $assetRoot $asset.filename
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Release asset is missing: $($asset.filename)"
    }
    $file = Get-Item -LiteralPath $path
    if ($file.Length -ne [Int64]$asset.sizeBytes) {
        throw "Size mismatch for $($asset.filename): manifest=$($asset.sizeBytes), actual=$($file.Length)"
    }
    if ((Get-Sha256 $path) -ne $asset.sha256.ToLowerInvariant()) {
        throw "SHA-256 mismatch for $($asset.filename). Regenerate manifest before publishing."
    }
    $uploadPaths.Add($path)
}

if ($ValidateOnly) {
    Write-Host "Validated $($uploadPaths.Count) model files and release URLs locally. Nothing published."
    return
}
Require-Command gh

if (-not $SkipRemoteVerification) {
    gh auth status | Out-Host
}

$title = "Parallel Finder models $Tag"
$notes = @"
Pose ONNX assets for Parallel Finder $Tag.

The application verifies every download against manifest.json before installation.
Source checkpoints are intentionally not included; this release contains ONNX runtime assets only.
"@

$existing = gh release view $Tag --repo $Repository 2>$null
if (-not $existing) {
    $createArgs = @('release', 'create', $Tag, '--repo', $Repository, '--title', $title, '--notes', $notes)
    if ($Prerelease) { $createArgs += '--prerelease' }
    if ($PSCmdlet.ShouldProcess("$Repository/$Tag", 'Create GitHub Release')) {
        & gh @createArgs
        if ($LASTEXITCODE -ne 0) { throw "gh release create failed with exit code $LASTEXITCODE" }
    }
}

if ($PSCmdlet.ShouldProcess("$Repository/$Tag", "Upload $($uploadPaths.Count) verified release assets")) {
    & gh release upload $Tag --repo $Repository @uploadPaths --clobber
    if ($LASTEXITCODE -ne 0) { throw "gh release upload failed with exit code $LASTEXITCODE" }
    # Publish the catalog only after every referenced model has uploaded.
    & gh release upload $Tag --repo $Repository $manifestPath --clobber
    if ($LASTEXITCODE -ne 0) { throw "manifest upload failed with exit code $LASTEXITCODE" }
    Write-Host "Published $($uploadPaths.Count) models and manifest to https://github.com/$Repository/releases/tag/$Tag"
}
