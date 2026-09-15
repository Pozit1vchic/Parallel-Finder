param(
    [Parameter(Mandatory = $true)] [string]$Token,
    [string]$Owner = "Pozit1vchic",
    [string]$Repository = "Parallel-Finder",
    [string]$Tag = "v0.1.0-models",
    [string]$Title = "Parallel Finder model catalog",
    [string]$AssetsDirectory = "D:\\Parallel-Finder\\release-models",
    [switch]$Draft
)

$ErrorActionPreference = "Stop"
$manifest = Join-Path $AssetsDirectory "manifest.json"
if (-not (Test-Path -LiteralPath $manifest)) {
    throw "manifest.json not found in $AssetsDirectory. Run tools/model_export/export_pose_release.py first."
}
$assets = @(Get-ChildItem -LiteralPath $AssetsDirectory -File |
    Where-Object { $_.Extension -in @('.onnx', '.json', '.zip', '.7z') })
if ($assets.Count -eq 0) { throw "No release assets found in $AssetsDirectory" }

$headers = @{
    Authorization = "Bearer $Token"
    Accept = "application/vnd.github+json"
    'X-GitHub-Api-Version' = '2022-11-28'
    UserAgent = 'ParallelFinder-release-publisher'
}
$api = "https://api.github.com/repos/$Owner/$Repository"
$payload = @{
    tag_name = $Tag
    target_commitish = 'main'
    name = $Title
    body = "Model/runtime assets for Parallel Finder. SHA-256 values are in manifest.json."
    draft = [bool]$Draft
    prerelease = $false
} | ConvertTo-Json

$release = Invoke-RestMethod -Method Post -Uri "$api/releases" -Headers $headers `
    -ContentType 'application/json' -Body $payload
try {
    foreach ($asset in $assets) {
        $upload = $release.upload_url -replace '\{\?name,label\}', "?name=$([uri]::EscapeDataString($asset.Name))"
        Write-Host "Uploading $($asset.Name) ..."
        Invoke-RestMethod -Method Post -Uri $upload -Headers $headers `
            -ContentType 'application/octet-stream' -InFile $asset.FullName | Out-Null
    }
} catch {
    Write-Warning "Asset upload failed; deleting the empty release to avoid a misleading published version."
    Invoke-RestMethod -Method Delete -Uri "$api/releases/$($release.id)" -Headers $headers
    throw
}
Write-Host "Published: $($release.html_url)"
