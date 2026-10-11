param(
    [Parameter(Mandatory=$true)][string[]]$Video,
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Model,
    [Parameter(Mandatory=$true)][string]$OrtDll,
    [string]$ReIdModel = '',
    [ValidateSet('cpu','cuda','tensorrt','dml')][string]$Provider = 'cuda',
    [ValidateSet('fast','medium','maximum')][string]$Quality = 'fast',
    [ValidateRange(1,10)][int]$Repeat = 1,
    [string]$OutputDirectory = "$PSScriptRoot/../../build/performance",
    [switch]$Serial
)
$ErrorActionPreference = 'Stop'
# Never clear the user's cache. Every cold run, including repeat measurements,
# gets a new directory. Reports and caches stay outside tracked source files.
$runRoot = Join-Path $OutputDirectory ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $runRoot)
$runRoot = (Resolve-Path -LiteralPath $runRoot).Path
$names = @('PF_DEBUG_ANALYSIS','PF_QUALITY_PROFILE','PF_EXPANDED_SEARCH','PF_BENCHMARK_CACHE_ROOT',
    'PF_ANALYSIS_PREFETCH','PF_DEBUG_POSES_JSON','PF_DEBUG_INFERENCE','PF_CUDA_CONV_SEARCH','PF_TRT_CACHE_PATH',
    'PF_MODEL_PATH','PF_ORT_DLL','PF_REID_MODEL_PATH','PF_ANALYSIS_MODE')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:PF_DEBUG_ANALYSIS = '1'
    $env:PF_QUALITY_PROFILE = $Quality
    $env:PF_EXPANDED_SEARCH = '0'
    $env:PF_DEBUG_POSES_JSON = '' # large diagnostic dumps would skew timing
    $env:PF_DEBUG_INFERENCE = '' # model profiling is a separate diagnostic, not the default timing run
    $env:PF_CUDA_CONV_SEARCH = '' # compare the production tuning policy, not a shell's experiment
    # Keep engine builds away from the user's accelerator cache too. Engines
    # are reused between passes; observation/preview caches are always empty.
    $env:PF_TRT_CACHE_PATH = Join-Path $runRoot 'tensorrt-engines'
    $env:PF_ANALYSIS_PREFETCH = if ($Serial) { '0' } else { '' } # application's provider-aware default
    for ($pass=1; $pass -le $Repeat; ++$pass) {
        for ($index=0; $index -lt $Video.Count; ++$index) {
            $stem = 'video-{0:D2}-pass-{1:D2}' -f $index,$pass
            $env:PF_BENCHMARK_CACHE_ROOT = Join-Path $runRoot "$stem-cache"
            $reportPath = Join-Path $runRoot "$stem.json"
            & "$PSScriptRoot/run_analysis_smoke.ps1" -Video $Video[$index] -Exe $Exe -Model $Model -OrtDll $OrtDll `
                -ReIdModel $ReIdModel -Provider $Provider -QpaPlatform windows -Modes @('combined') `
                -TimeoutSec 3600 -ReportPath $reportPath 3>(Join-Path $runRoot "$stem.log") 6>$null
            $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
            if ($report.timingDiagnostics -match 'complete_cache_hit=1') { throw 'Cold benchmark reused an observation cache.' }
            [pscustomobject]@{video=$Video[$index]; pass=$pass; elapsedMs=$report.elapsedMs; cpuTimeMs=$report.cpuTimeMs;
                pairs=@($report.results).Count; frames=$report.frameCount; report=$reportPath;
                stages=$report.timingDiagnostics} | ConvertTo-Json -Depth 4 -Compress
        }
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
