param(
    [Parameter(Mandatory = $true)]
    [string]$Video,
    [string]$Exe = "D:\\Parallel-Finder\\build\\ucrt64-release\\ParallelFinder.exe",
    [string]$Model = "D:\\PF_CUDA\\models\\yolo26m-pose-640-b1.onnx",
    [string]$OrtDll = "D:\\msys2\\ucrt64\\bin\\onnxruntime.dll",
    [string]$ReIdModel = "",
    [ValidateRange(1, 3600)][int]$TimeoutSec = 120,
    [ValidateRange(0, 10000)][int]$MinimumPairs = 0,
    [double]$ExpectedOffsetSec = -1,
    [string]$ReportPath = "",
    [string[]]$Modes = @("motion", "static", "clips", "combined")
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { throw "Executable not found: $Exe" }
if (-not (Test-Path -LiteralPath $Video)) { throw "Video not found: $Video" }
if (-not (Test-Path -LiteralPath $Model)) { throw "Pose model not found: $Model" }
if (-not (Test-Path -LiteralPath $OrtDll)) { throw "ONNX Runtime DLL not found: $OrtDll" }
if ($ReIdModel -and -not (Test-Path -LiteralPath $ReIdModel)) { throw "ReID model not found: $ReIdModel" }

$env:PF_MODEL_PATH = (Resolve-Path -LiteralPath $Model).Path
$env:PF_ORT_DLL = (Resolve-Path -LiteralPath $OrtDll).Path
if ($ReIdModel) { $env:PF_REID_MODEL_PATH = (Resolve-Path -LiteralPath $ReIdModel).Path }

Write-Host "Parallel Finder analysis benchmark (pipeline smoke, not accuracy ground truth)"
Write-Host "video: $Video"
Write-Host "model: $env:PF_MODEL_PATH"
Write-Host "runtime: $env:PF_ORT_DLL"
if ($env:PF_REID_MODEL_PATH) { Write-Host "ReID: $env:PF_REID_MODEL_PATH" }

foreach ($mode in $Modes) {
    if ($mode -notin @("motion", "static", "clips", "combined")) {
        throw "Unsupported mode '$mode'"
    }
    $env:PF_ANALYSIS_MODE = $mode
    Write-Host "`n--- mode: $mode ---"
    # Start the GUI-subsystem executable directly and capture its stdout. A
    # plain PowerShell call can detach GUI applications on Windows, leaving
    # `$LASTEXITCODE` empty and hiding the actual result.
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.Arguments = '--pf-analysis-smoke "' + $Video.Replace('"', '\\"') + '"'
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.EnvironmentVariables['QT_QPA_PLATFORM'] = 'offscreen'
    $psi.EnvironmentVariables['PF_ORT_DLL'] = $env:PF_ORT_DLL
    $psi.EnvironmentVariables['PF_MODEL_PATH'] = $env:PF_MODEL_PATH
    $psi.EnvironmentVariables['PF_PROVIDER'] = 'cpu'
    $psi.EnvironmentVariables['PF_ANALYSIS_TIMEOUT_SEC'] = [string]$TimeoutSec
    $psi.EnvironmentVariables['PF_ANALYSIS_MIN_PAIRS'] = [string]$MinimumPairs
    $psi.EnvironmentVariables['PF_ANALYSIS_JSON'] = '1'
    if ($env:PF_DEBUG_ANALYSIS) { $psi.EnvironmentVariables['PF_DEBUG_ANALYSIS'] = $env:PF_DEBUG_ANALYSIS }
    if ($env:PF_DEBUG_POSE) { $psi.EnvironmentVariables['PF_DEBUG_POSE'] = $env:PF_DEBUG_POSE }
    if ($env:PF_DEBUG_MATCHER) { $psi.EnvironmentVariables['PF_DEBUG_MATCHER'] = $env:PF_DEBUG_MATCHER }
    if ($env:PF_REID_MODEL_PATH) { $psi.EnvironmentVariables['PF_REID_MODEL_PATH'] = $env:PF_REID_MODEL_PATH }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    [void]$process.Start()
    # Read both redirected streams concurrently. Reading stdout to completion
    # before stderr can deadlock a verbose PF_DEBUG_MATCHER run when stderr's
    # pipe fills before the child has a chance to exit.
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    Write-Host $stdout
    if ($stderr) { Write-Warning $stderr }
    if ($process.ExitCode -ne 0) {
        throw "Analysis smoke failed for mode '$mode' (exit $($process.ExitCode))"
    }
    if ($ExpectedOffsetSec -ge 0 -or $ReportPath) {
        $jsonLine = ($stdout -split '\r?\n' | Where-Object { $_ -match '^\s*results_json :' } | Select-Object -First 1)
        if (-not $jsonLine) { throw 'Missing results JSON; rebuild the executable.' }
        $pairs = @((($jsonLine -replace '^\s*results_json :\s*', '') | ConvertFrom-Json))
        if ($ReportPath) {
            $report = @{ video=$Video; mode=$mode; results=$pairs }
            $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
        }
    }
    if ($ExpectedOffsetSec -ge 0) {
        $correct = @($pairs | Where-Object {
            [Math]::Abs(($_.rightStart - $_.leftStart) - $ExpectedOffsetSec) -le 0.5
        })
        if ($correct.Count -eq 0) { throw "No pair has the expected repeat offset $ExpectedOffsetSec seconds." }
    }
    $process.Dispose()
}

Remove-Item Env:PF_ANALYSIS_MODE -ErrorAction SilentlyContinue
Write-Host "`nCompleted. Compare files/frames/pairs and elapsed time manually; no precision claim is made without labelled ground truth."
