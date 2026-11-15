param(
    [Parameter(Mandatory)][string]$Video,
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$Model,
    [Parameter(Mandatory)][string]$OrtDll,
    [Parameter(Mandatory)][string]$ReIdModel,
    [ValidateSet('cpu','cuda')][string]$Provider = 'cuda',
    [string]$OutputDirectory = "$PSScriptRoot/../../build/full-analysis"
)
$ErrorActionPreference = 'Stop'
$run = (New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory ([guid]::NewGuid().ToString('N')))).FullName
$settings = Join-Path $run 'settings.json'
@{provider=$Provider; qualityProfile='fast'; analysisMode='combined'; cacheEnabled=$false; processingThreads=0; expandedSearch=$false} | ConvertTo-Json | Set-Content $settings
$envValues = @{
    PF_BENCHMARK_SETTINGS_PATH=$settings
    PF_BENCHMARK_CACHE_ROOT=(Join-Path $run 'cache')
    PF_BENCHMARK_MOVEMENT_CACHE_ROOT=(Join-Path $run 'movement')
    PF_DEBUG_ANALYSIS='1'
    PF_SMOKE_ALL_PEOPLE='1'
    PF_MODEL_ROOT=(Split-Path (Resolve-Path -LiteralPath $ReIdModel).Path)
    PF_SMOKE_TARGET_ID=$null
    PF_DEBUG_POSES_JSON=$null
    PF_QUALITY_PROFILE=$null
    PF_EXPANDED_SEARCH=$null
    PF_VIDEO_DECODE=$null
    PF_ANALYSIS_PREFETCH=$null
    PF_AUDIT_NATIVE_CANDIDATES=$null
}
$saved = @{}
$reportPath = Join-Path $run 'analysis.json'
Write-Host "Report directory: $run"
try {
    foreach ($name in $envValues.Keys) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $envValues[$name], 'Process')
    }
    $options = @{Video=$Video; Exe=$Exe; Model=$Model; OrtDll=$OrtDll; ReIdModel=$ReIdModel; Provider=$Provider
        Modes=@('combined'); TimeoutSec=3600; ReportPath=$reportPath; ResourceTelemetry=$true}
    & "$PSScriptRoot/run_analysis_smoke.ps1" @options *> (Join-Path $run 'analysis.log')
} finally {
    foreach ($name in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
    }
}
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if (@($report.inputVideos).Count -ne 1 -or !$report.workCounters.analyzed_frames -or $report.decodeDiagnostics -notmatch 'cache_hit=0 ') {
    throw 'Expected a full uncached analysis of one source'
}
$source = @($report.sourceSummaries.PSObject.Properties.Value)
if ($source.Count -ne 1 -or $source[0].observationCacheHit -ne $false) {
    throw 'Missing uncached source summary'
}
$metrics = @{}
foreach ($line in @($report.timingDiagnostics) + @($report.decodeDiagnostics)) {
    foreach ($match in [regex]::Matches($line, '(?<!\S)([a-z_]+)=([0-9]+(?:\.[0-9]+)?)(?!\S)')) {
        $metrics[$match.Groups[1].Value] = [double]::Parse($match.Groups[2].Value, [cultureinfo]::InvariantCulture)
    }
}
$stages = [ordered]@{}
foreach ($key in 'decode_infer_ms','scene_ms','windows_ms','matcher_ms','rank_ms','postrank_total_ms') {
    if (!$metrics.ContainsKey($key)) { throw "Missing timing metric: $key" }
    $stages[$key] = $metrics[$key]
}
[ordered]@{
    source=$report.inputVideos[0]; providerRequested=$Provider; mode='combined, all people, fast'
    sourceDurationSeconds=$source[0].durationSeconds; estimatedSourceFrames=$source[0].frameCount
    decodeDiagnostics=$report.decodeDiagnostics
    elapsedMs=$report.elapsedMs; work=$report.workCounters; pairs=@($report.results).Count; stagesMs=$stages
    largestMeasuredStage=($stages.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key
    metrics=$metrics; report=$reportPath
} | ConvertTo-Json -Depth 6 | Tee-Object -FilePath (Join-Path $run 'summary.json')
