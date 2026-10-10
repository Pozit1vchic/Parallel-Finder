#requires -Version 7.0
param(
    [string]$BuildDirectory = 'build/ucrt64-release',
    [string]$OutputPath = 'build/audit-checks.md',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $root
try {
    $tracked = @(& git ls-files --cached --others --exclude-standard | Sort-Object -Unique | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($LASTEXITCODE) { throw 'Cannot enumerate tracked files' }
    $sources = @($tracked | Where-Object { $_ -match '\.(cpp|hpp|h|qml)$' })
    $qml = @($sources | Where-Object { $_ -match '\.qml$' })
    $forbidden = @($qml | Select-String -Pattern 'Georgia|Verdana|Times New Roman|Calibri|Arial')
    $noise = @($tracked | Where-Object {
        $_ -match '\.(log|tmp|bak|dll|exe|onnx|pt|pfc)$' -and $_ -notlike 'tests/fixtures/*'
    })
    $parserErrors = @()
    foreach ($script in @($tracked | Where-Object { $_ -match '\.ps1$' })) {
        if (!(Test-Path -LiteralPath $script -PathType Leaf)) { continue }
        $tokens = $null
        $errors = $null
        [System.Management.Automation.Language.Parser]::ParseFile(
            (Join-Path $root $script), [ref]$tokens, [ref]$errors) | Out-Null
        foreach ($error in $errors) { $parserErrors += "${script}: $($error.Message)" }
    }
    $brokenDocs = @()
    foreach ($document in @($tracked | Where-Object { $_ -match '\.md$' })) {
        $content = Get-Content -LiteralPath $document -Raw -Encoding utf8
        foreach ($match in [regex]::Matches($content, '\[[^\]]+\]\(([^)]+)\)')) {
            $target = $match.Groups[1].Value.Trim('<', '>')
            if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $target.StartsWith('#')) { continue }
            $path = [Uri]::UnescapeDataString(($target -split '#', 2)[0])
            if (!$path) { continue }
            $parent = Split-Path (Join-Path $root $document)
            if (!(Test-Path -LiteralPath (Join-Path $parent $path))) { $brokenDocs += "${document}: $target" }
        }
    }
    & git diff --check
    $diffPassed = $LASTEXITCODE -eq 0
    $testOutput = @('Tests skipped by explicit -SkipTests option.')
    $testPassed = $null
    if (!$SkipTests) {
        $testOutput = @(& ctest --test-dir $BuildDirectory --output-on-failure 2>&1)
        $testPassed = $LASTEXITCODE -eq 0
    }
    $date = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(3)).ToString('yyyy-MM-dd HH:mm:ss zzz')
    $revision = & git rev-parse --short HEAD
    $state = if ($SkipTests) { 'SKIPPED' } elseif ($testPassed) { 'PASS' } else { 'FAIL' }
    $report = @"
# Automated repository checks

Generated: $date (Moscow). Git revision: $revision; working-tree changes may be present.

- Tracked C++/QML source files: $($sources.Count); QML: $($qml.Count).
- Forbidden font references: $($forbidden.Count).
- Tracked generated files/logs/weights outside test fixtures: $($noise.Count).
- PowerShell syntax errors: $($parserErrors.Count).
- Broken local Markdown links: $($brokenDocs.Count).
- git diff --check: $(if ($diffPassed) { 'PASS' } else { 'FAIL' }).
- CTest: $state.

``````text
$($testOutput -join "`n")
``````

## Findings from these checks

$(@($noise + $parserErrors + $brokenDocs + @($forbidden | ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" })) -join "`n")

This report contains only the checks executed above. It does not measure matcher
precision/recall, GUI resource leaks, runtime installation, or clean-machine
compatibility. The reviewed findings and remaining work are recorded in AUDIT.md.
"@
    $absoluteOutput = if ([IO.Path]::IsPathRooted($OutputPath)) { [IO.Path]::GetFullPath($OutputPath) }
                      else { [IO.Path]::GetFullPath((Join-Path $root $OutputPath)) }
    New-Item -ItemType Directory -Force -Path (Split-Path $absoluteOutput) | Out-Null
    $report | Set-Content -LiteralPath $absoluteOutput -Encoding utf8
    Write-Output "REPORT=$absoluteOutput"
    if ($testPassed -eq $false -or !$diffPassed -or $parserErrors.Count -gt 0 -or $noise.Count -gt 0 -or $brokenDocs.Count -gt 0 -or $forbidden.Count -gt 0) {
        throw 'Repository checks failed; see the generated report.'
    }
} finally {
    Pop-Location
}
