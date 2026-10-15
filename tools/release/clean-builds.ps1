param(
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
$workspaceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$buildRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot 'build'))
if (!(Test-Path -LiteralPath $buildRoot -PathType Container)) { return }
foreach ($root in @($workspaceRoot, $buildRoot)) {
    if ((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Cleanup refuses a linked root: $root"
    }
}
$buildPrefix = $buildRoot + [IO.Path]::DirectorySeparatorChar
$activeExecutables = @(Get-Process -ErrorAction SilentlyContinue | ForEach-Object {
    try { if ($_.Path) { $_.Path } } catch { }
})
$modelRoots = @((Join-Path $workspaceRoot 'models'), (Join-Path ([Environment]::GetFolderPath('Desktop')) 'ParallelFinder/models'))
$candidates = @(Get-ChildItem -LiteralPath $buildRoot -Directory | Where-Object { $_.Name -match '^(package|source)-[a-f0-9]{8}$' })
foreach ($candidate in $candidates) {
    $absolute = [IO.Path]::GetFullPath($candidate.FullName)
    if (!$absolute.StartsWith($buildPrefix,[StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetDirectoryName($absolute) -ne $buildRoot) { throw 'Cleanup target escaped build directory' }
    if ($candidate.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        Write-Warning "Skipping linked directory: $absolute"; continue
    }
    $entries = @(Get-ChildItem -LiteralPath $absolute -Force -Recurse)
    if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        Write-Warning "Skipping directory containing links: $absolute"; continue
    }
    $targetPrefix = $absolute + [IO.Path]::DirectorySeparatorChar
    if (@($activeExecutables | Where-Object { $_.StartsWith($targetPrefix,[StringComparison]::OrdinalIgnoreCase) }).Count) {
        Write-Warning "Skipping running package: $absolute"; continue
    }
    # Every model copy must also exist outside the disposable staging folder.
    $modelsPreserved = $true
    foreach ($model in @($entries | Where-Object { !$_.PSIsContainer -and $_.Extension -eq '.onnx' })) {
        $copies = @($modelRoots | ForEach-Object { Join-Path $_ $model.Name })
        if ($candidate.Name.StartsWith('source-')) {
            $copies += Join-Path $workspaceRoot ('tests/fixtures/' + $model.Name)
        }
        $modelHash = (Get-FileHash -LiteralPath $model.FullName -Algorithm SHA256).Hash
        $found = $false
        foreach ($copy in $copies) {
            if ((Test-Path -LiteralPath $copy -PathType Leaf) -and
                (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -eq $modelHash) { $found = $true; break }
        }
        if (!$found) { $modelsPreserved = $false; break }
    }
    if (!$modelsPreserved) { Write-Warning "Skipping directory with an unpreserved model: $absolute"; continue }
    $bytes = ($entries | Where-Object { !$_.PSIsContainer } | Measure-Object Length -Sum).Sum
    [pscustomobject]@{Path=$absolute;Bytes=$bytes;Action=$(if($Apply){'Remove'}else{'Preview'})}
    if ($Apply) { Remove-Item -LiteralPath $absolute -Recurse -Force }
}
# Keep the current compiler output, all unknown folders, releases, datasets,
# dependencies and user caches. Empty output means there are no staging copies.
