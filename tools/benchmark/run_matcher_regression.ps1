param(
    [Parameter(Mandatory = $true)][string]$Video,
    [Parameter(Mandatory = $true)][string]$Model,
    [Parameter(Mandatory = $true)][string]$ReIdModel,
    [string]$Exe = "D:\Parallel-Finder\build\ucrt64-release\ParallelFinder.exe",
    [string]$OrtDll = "D:\msys2\ucrt64\bin\onnxruntime.dll",
    [ValidateRange(0, 100000)][double]$StartSec = 0
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $Video).Path
$fixtureDir = Join-Path ([System.IO.Path]::GetTempPath()) ('pf-matcher-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureDir)
$fixture = Join-Path $fixtureDir 'repeat.mp4'
# Known positive: the same three-second movement appears at t=0 and t=8.
# Choose a source interval with a visible moving person. This is a pipeline
# regression, not a precision/recall benchmark on independently filmed actions.
$start = $StartSec.ToString([Globalization.CultureInfo]::InvariantCulture)
$filter = "[0:v]trim=start=${start}:duration=3,setpts=PTS-STARTPTS,fps=12,scale=640:360,setsar=1,split[a][b];color=c=black:s=640x360:r=12:d=5[g];[a][g][b]concat=n=3:v=1:a=0[v]"
& ffmpeg -hide_banner -loglevel error -n -i $source -filter_complex $filter -map '[v]' -an -c:v libx264 -preset fast -crf 20 $fixture
if ($LASTEXITCODE -ne 0) { throw 'Failed to create regression fixture.' }
& "$PSScriptRoot/run_analysis_smoke.ps1" -Video $fixture -Exe $Exe -Model $Model -OrtDll $OrtDll -ReIdModel $ReIdModel -Modes motion -MinimumPairs 1 -ExpectedOffsetSec 8
Write-Host "Regression passed. Fixture retained for inspection: $fixture"
