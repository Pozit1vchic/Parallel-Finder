param(
    [Parameter(Mandatory=$true)][string]$Video,
    [Parameter(Mandatory=$true)][string]$CacheRoot,
    [Parameter(Mandatory=$true)][string]$WindowKeys,
    [Parameter(Mandatory=$true)][string]$Model,
    [Parameter(Mandatory=$true)][string]$ReIdModel,
    [Parameter(Mandatory=$true)][string]$OrtDll,
    [Parameter(Mandatory=$true)][string]$ReportPath,
    [string]$Exe='D:/Parallel-Finder/build/ucrt64-release/ParallelFinder.exe',
    [ValidateSet('0','1')][string]$ExpandedSearch='0',
    [ValidateRange(1,10000)][int]$MaxUniqueResults=500,
    [ValidateRange(1,43200)][int]$TimeoutSec=1200
)
$ErrorActionPreference='Stop'
$source=(Resolve-Path -LiteralPath $Video).Path.Replace('\','/')
$keys=Get-Content -LiteralPath $WindowKeys -Raw | ConvertFrom-Json -AsHashtable
if(-not $keys.ContainsKey($source)){throw "No frozen window key for source: $source"}
$key=[string]$keys[$source]
$digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($key))).ToLowerInvariant()
$cacheFile=Join-Path $CacheRoot ($digest+'.pfc')
if(-not(Test-Path -LiteralPath $cacheFile -PathType Leaf)){throw "Frozen motion cache missing: $cacheFile"}

# A frozen-observation run measures the complete matcher/result pipeline. It
# is deliberately separate from the first-analysis benchmark and must never
# fall back silently to a fresh hour of model inference.
$names=@('PF_DEBUG_ANALYSIS','PF_QUALITY_PROFILE','PF_BENCHMARK_CACHE_ROOT',
    'PF_AUDIT_REUSE_OBSERVATIONS','PF_AUDIT_FROZEN_WINDOW_KEYS','PF_EXPANDED_SEARCH','PF_MAX_UNIQUE_RESULTS')
$previous=@{}
foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
    $env:PF_DEBUG_ANALYSIS='1'
    $env:PF_QUALITY_PROFILE='fast'
    $env:PF_EXPANDED_SEARCH=$ExpandedSearch
    $env:PF_MAX_UNIQUE_RESULTS=[string]$MaxUniqueResults
    $env:PF_BENCHMARK_CACHE_ROOT=(Resolve-Path -LiteralPath $CacheRoot).Path
    $env:PF_AUDIT_REUSE_OBSERVATIONS='1'
    $env:PF_AUDIT_FROZEN_WINDOW_KEYS=(Resolve-Path -LiteralPath $WindowKeys).Path
    & "$PSScriptRoot/run_analysis_smoke.ps1" -Video $Video -Exe $Exe -Model $Model -ReIdModel $ReIdModel `
        -OrtDll $OrtDll -Provider cuda -Modes combined -TimeoutSec $TimeoutSec -ReportPath $ReportPath
    $log=Get-Content -LiteralPath ($ReportPath+'.stderr.log') -Raw
    if($log -notmatch 'complete_cache_hit=1 window_cache_hit=1 decoded=0 view_samples=0 pose_samples=0') {
        throw 'Frozen benchmark did not reuse complete observations; result is not a comparable matcher timing.'
    }
    if($log -notmatch 'legacy_window_replay=1 new_model_inference=0') {
        throw 'Expected frozen-window replay was not confirmed.'
    }
    $report=Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
    [pscustomobject]@{Scenario='frozen-observations';ElapsedMs=$report.elapsedMs;Pairs=$report.results.Count;Report=$ReportPath}
} finally {
    foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}
}
