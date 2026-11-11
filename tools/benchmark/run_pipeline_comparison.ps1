param(
    [Parameter(Mandatory=$true)][string]$BeforeExe,
    [Parameter(Mandatory=$true)][string]$AfterExe,
    [Parameter(Mandatory=$true)][string[]]$Videos,
    [Parameter(Mandatory=$true)][string]$Model,
    [Parameter(Mandatory=$true)][string]$ModelRoot,
    [Parameter(Mandatory=$true)][string]$ReIdModel,
    [Parameter(Mandatory=$true)][string]$OrtDll,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [ValidateRange(1,10)][int]$Repeats=5,
    [ValidateSet('cpu','cuda','tensorrt','dml')][string]$Provider='cuda',
    [ValidateSet('fast','medium','maximum')][string]$Quality='fast',
    [switch]$AllPeople
)
$ErrorActionPreference='Stop'
if(-not $Videos.Count){throw 'At least one source video is required.'}
if(Test-Path -LiteralPath $OutputRoot){throw 'Use a new output directory; existing evidence is not overwritten.'}
foreach($inputFile in @($BeforeExe,$AfterExe,$Model,$ReIdModel,$OrtDll)+$Videos){
    if(-not (Test-Path -LiteralPath $inputFile -PathType Leaf)){throw "Missing input: $inputFile"}
}
if(-not (Test-Path -LiteralPath $ModelRoot -PathType Container)){throw 'Missing model directory.'}
$outputDirectory=[IO.Path]::GetFullPath($OutputRoot)
[void][IO.Directory]::CreateDirectory($outputDirectory)
function Fingerprint([string]$path){
    $item=Get-Item -LiteralPath $path
    return @{path=$item.FullName;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$contract=@{
    schemaVersion=1;provider=$Provider;quality=$Quality;mode='motion';repeats=$Repeats;allPeople=[bool]$AllPeople
    order='Alternating before/after; cold then warm in a fresh cache per version/repeat'
    coldMeaning='Empty application cache; OS file cache and driver state are not reset'
    scope='Complete smoke analysis, actual admission rules; no ground-truth quality claim'
    logicalProcessors=[Environment]::ProcessorCount
    before=(Fingerprint $BeforeExe);after=(Fingerprint $AfterExe)
    model=(Fingerprint $Model);reid=(Fingerprint $ReIdModel);runtime=(Fingerprint $OrtDll)
    modelInventory=@(Get-ChildItem -LiteralPath $ModelRoot -Filter '*.onnx' | Sort-Object Name | ForEach-Object {Fingerprint $_.FullName})
    videos=@($Videos | ForEach-Object {Fingerprint $_})
    processVram='Unavailable when nvidia-smi reports N/A on this Windows driver; not inferred from global GPU memory'
}
$settingsPath=Join-Path $outputDirectory 'settings.json'
# Both binaries get the same explicit settings; user preferences are neither
# copied (which could expose tokens) nor changed. Remaining fields use defaults.
$settings=@{cacheEnabled=$true;processingThreads=4;qualityProfile=$Quality;analysisMode='motion';
    provider=$Provider;expandedSearch=$false;maxUniqueResults=100;
    modelPath=[IO.Path]::GetFullPath($Model);modelChoice=[IO.Path]::GetFileName($Model)}
$settings | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding utf8
$contract.settings=$settings
$contract.settingsFile=(Fingerprint $settingsPath)
$contract | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDirectory 'contract.json') -Encoding utf8
$taskVariableNames=@('PF_MODEL_ROOT','PF_QUALITY_PROFILE','PF_DEBUG_ANALYSIS','PF_BENCHMARK_CACHE_ROOT','PF_BENCHMARK_MOVEMENT_CACHE_ROOT','PF_BENCHMARK_SETTINGS_PATH','PF_SMOKE_ALL_PEOPLE','PF_SMOKE_TARGET_ID')
$previousEnvironment=@{}
foreach($variableName in $taskVariableNames){$previousEnvironment[$variableName]=[Environment]::GetEnvironmentVariable($variableName,'Process')}
$records=@()
try {
    $env:PF_MODEL_ROOT=[IO.Path]::GetFullPath($ModelRoot)
    $env:PF_QUALITY_PROFILE=$Quality
    $env:PF_DEBUG_ANALYSIS='1'
    $env:PF_BENCHMARK_SETTINGS_PATH=$settingsPath
    $env:PF_SMOKE_ALL_PEOPLE=if($AllPeople){'1'}else{'0'}
    $env:PF_SMOKE_TARGET_ID=$null
    for($repeat=0;$repeat -lt $Repeats;++$repeat){
        $versions=if($repeat%2 -eq 0){@('before','after')}else{@('after','before')}
        foreach($version in $versions){
            $exe=if($version -eq 'before'){$BeforeExe}else{$AfterExe}
            $env:PF_BENCHMARK_CACHE_ROOT=Join-Path $outputDirectory "$version-$repeat-cache"
            $env:PF_BENCHMARK_MOVEMENT_CACHE_ROOT=Join-Path $outputDirectory "$version-$repeat-refinement"
            foreach($temperature in @('cold','warm')){
                $stem="$version-$repeat-$temperature"
                $reportPath=Join-Path $outputDirectory "$stem.json"
                $smokeArguments=@{
                    Exe=$exe;Video=$Videos[0];AdditionalVideos=@($Videos | Select-Object -Skip 1)
                    Model=$Model;ReIdModel=$ReIdModel;OrtDll=$OrtDll;Provider=$Provider
                    Modes=@('motion');TimeoutSec=300;ReportPath=$reportPath
                }
                & "$PSScriptRoot/run_analysis_smoke.ps1" @smokeArguments *> (Join-Path $outputDirectory "$stem.log")
                if(-not (Test-Path -LiteralPath $reportPath)){throw "Missing successful report: $stem"}
                $report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
                if($report.memorySamples -lt 1 -or $report.peakWorkingSetBytes -le 0){throw "No live memory samples: $stem"}
                $records+=@{version=$version;repeat=$repeat;temperature=$temperature;report=[IO.Path]::GetFileName($reportPath)
                    elapsedMs=$report.elapsedMs;processWallMs=$report.processWallMs;cpuTimeMs=$report.cpuTimeMs
                    peakWorkingSetBytes=$report.peakWorkingSetBytes;sampledPeakPrivateBytes=$report.sampledPeakPrivateBytes
                    frameCount=$report.frameCount;resultCount=@($report.results).Count}
                $records | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outputDirectory 'runs.json') -Encoding utf8
                Write-Host "$stem elapsed=$($report.elapsedMs)ms peakWorkingSet=$($report.peakWorkingSetBytes)B"
            }
        }
    }
} finally {
    foreach($variableName in $taskVariableNames){[Environment]::SetEnvironmentVariable($variableName,$previousEnvironment[$variableName],'Process')}
}
