param(
    [Parameter(Mandatory=$true)][string]$Video,
    [Parameter(Mandatory=$true)][string]$Model,
    [Parameter(Mandatory=$true)][string]$OrtDll,
    [Parameter(Mandatory=$true)][string]$ReIdModel,
    [string]$Exe = "$PSScriptRoot/../../build/ucrt64-release/ParallelFinder.exe",
    [string]$Ffmpeg = "$PSScriptRoot/../../build/ucrt64-release/ffmpeg.exe",
    [ValidateSet('offscreen','windows')][string]$QpaPlatform = 'offscreen',
    [string]$OutputDirectory = "$PSScriptRoot/../../build/mask-quality"
)
$ErrorActionPreference = 'Stop'
# These coordinates and anchors belong only to the 55-second 4K Dean test
# scenepack, never to the production matcher. No original media is changed.
if (!(Test-Path -LiteralPath $Video)) { throw "Missing Dean test scenepack: $Video" }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$a = Join-Path $OutputDirectory 'masked-folded-a.mp4'
$b = Join-Path $OutputDirectory 'masked-folded-b.mp4'
& $Ffmpeg -hide_banner -loglevel error -ss 30.95 -i $Video -t 2.5 -vf 'scale=1280:720,drawbox=x=445:y=105:w=265:h=180:color=black:t=fill' -an -c:v libx264 -preset ultrafast -crf 20 -y $a
if ($LASTEXITCODE) { throw 'Failed to create masked control A.' }
& $Ffmpeg -hide_banner -loglevel error -ss 33.80 -i $Video -t 1.55 -vf 'scale=1280:720,drawbox=x=350:y=130:w=220:h=160:color=black:t=fill' -an -c:v libx264 -preset ultrafast -crf 20 -y $b
if ($LASTEXITCODE) { throw 'Failed to create masked control B.' }
$savedCostume = $env:PF_COSTUME_MODE
$savedQuality = $env:PF_QUALITY_PROFILE
$report = Join-Path $OutputDirectory 'report.json'
try {
    $env:PF_COSTUME_MODE = '1'
    $env:PF_QUALITY_PROFILE = 'fast'
    & "$PSScriptRoot/run_analysis_smoke.ps1" -Video $a -AdditionalVideos @($b) -Exe $Exe -QpaPlatform $QpaPlatform -Model $Model -OrtDll $OrtDll -ReIdModel $ReIdModel -Provider cuda -Modes @('combined') -TimeoutSec 120 -ReportPath $report
    $results = @((Get-Content -LiteralPath $report -Raw | ConvertFrom-Json).results)
    $positive = @($results | Where-Object {
        $_.matchType -eq 'pose' -and $_.identityVerified -and $_.costumeMode -and !$_.faceVerified
    })
    if (!$positive.Count) { throw 'Masked folded-arms control was missed.' }
    if (@($results | Where-Object faceVerified).Count) { throw 'Costume test unexpectedly used face evidence.' }
    Write-Host 'PASS: physically occluded faces, observed folded arms, body identity, no face evidence.'
    Write-Host 'This small positive control does not prove accuracy for arbitrary costumes or different actors.'
} finally {
    $env:PF_COSTUME_MODE = $savedCostume
    $env:PF_QUALITY_PROFILE = $savedQuality
}
