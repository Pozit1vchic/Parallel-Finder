param(
    [Parameter(Mandatory = $true)]
    [string]$Video,
    [string]$Exe = "D:\\Parallel-Finder\\build\\ucrt64-release\\ParallelFinder.exe",
    [string]$Model = "D:\\PF_CUDA\\models\\yolo26m-pose-640-b1.onnx",
    [string]$OrtDll = "D:\\msys2\\ucrt64\\bin\\onnxruntime.dll",
    [string]$ReIdModel = "",
    [string[]]$AdditionalVideos = @(),
    [ValidateSet('offscreen','windows')][string]$QpaPlatform = 'windows',
    [ValidateSet('cpu','cuda','tensorrt','dml')][string]$Provider = 'cpu',
    [ValidateRange(1, 43200)][int]$TimeoutSec = 120,
    [ValidateRange(0, 10000)][int]$MinimumPairs = 0,
    [double]$ExpectedOffsetSec = -1,
    [string]$ReportPath = "",
    [string[]]$Modes = @("motion", "static", "combined")
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { throw "Executable not found: $Exe" }
if (-not (Test-Path -LiteralPath $Video)) { throw "Video not found: $Video" }
foreach ($additionalVideo in $AdditionalVideos) {
    if (-not (Test-Path -LiteralPath $additionalVideo)) { throw "Video not found: $additionalVideo" }
}
if (-not (Test-Path -LiteralPath $Model)) { throw "Pose model not found: $Model" }
if (-not (Test-Path -LiteralPath $OrtDll)) { throw "ONNX Runtime DLL not found: $OrtDll" }
if ($ReIdModel -and -not (Test-Path -LiteralPath $ReIdModel)) { throw "ReID model not found: $ReIdModel" }

# Explicit choices belong to the child only. Leaving a TensorRT DLL in the
# caller's environment can contaminate a subsequent CPU/QML test process
# without the matching provider-root DLL search path.
$smokeModelPath = (Resolve-Path -LiteralPath $Model).Path
$smokeOrtPath = (Resolve-Path -LiteralPath $OrtDll).Path
$smokeReIdPath = if ($ReIdModel) { (Resolve-Path -LiteralPath $ReIdModel).Path } else { $env:PF_REID_MODEL_PATH }

Write-Host "Parallel Finder analysis benchmark (pipeline smoke, not accuracy ground truth)"
Write-Host "video: $Video"
Write-Host "model: $smokeModelPath"
Write-Host "runtime: $smokeOrtPath"
if ($smokeReIdPath) { Write-Host "ReID: $smokeReIdPath" }

foreach ($mode in $Modes) {
    if ($mode -notin @("motion", "static", "combined")) {
        throw "Unsupported mode '$mode'"
    }
    Write-Host "`n--- mode: $mode ---"
    # Start the GUI-subsystem executable directly and capture its stdout. A
    # plain PowerShell call can detach GUI applications on Windows, leaving
    # `$LASTEXITCODE` empty and hiding the actual result.
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.Arguments = '--pf-analysis-smoke ' + ((@($Video) + $AdditionalVideos | ForEach-Object {
        '"' + $_.Replace('"', '\\"') + '"'
    }) -join ' ')
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $psi.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    # Packaged Windows builds ship qwindows, not the developer offscreen
    # plugin. Analysis smoke creates no window; -QpaPlatform windows permits
    # testing that actual package without injecting developer Qt plugins.
    $psi.EnvironmentVariables['QT_QPA_PLATFORM'] = $QpaPlatform
    $psi.EnvironmentVariables['PF_ORT_DLL'] = $smokeOrtPath
    # cuDNN/TensorRT load additional DLLs lazily. Mirror application startup
    # when explicitly selecting an external runtime for the benchmark.
    $psi.EnvironmentVariables['PF_PROVIDER_ROOT'] = Split-Path -Parent $smokeOrtPath
    $psi.EnvironmentVariables['PF_MODEL_PATH'] = $smokeModelPath
    $psi.EnvironmentVariables['PF_ANALYSIS_MODE'] = $mode
    $psi.EnvironmentVariables['PF_PROVIDER'] = $Provider
    $psi.EnvironmentVariables['PF_ANALYSIS_TIMEOUT_SEC'] = [string]$TimeoutSec
    $psi.EnvironmentVariables['PF_ANALYSIS_MIN_PAIRS'] = [string]$MinimumPairs
    $psi.EnvironmentVariables['PF_ANALYSIS_JSON'] = '1'
    if ($env:PF_DEBUG_ANALYSIS) { $psi.EnvironmentVariables['PF_DEBUG_ANALYSIS'] = $env:PF_DEBUG_ANALYSIS }
    if ($env:PF_DEBUG_POSE) { $psi.EnvironmentVariables['PF_DEBUG_POSE'] = $env:PF_DEBUG_POSE }
    if ($env:PF_DEBUG_MATCHER) { $psi.EnvironmentVariables['PF_DEBUG_MATCHER'] = $env:PF_DEBUG_MATCHER }
    if ($smokeReIdPath) { $psi.EnvironmentVariables['PF_REID_MODEL_PATH'] = $smokeReIdPath }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    $processClock = [System.Diagnostics.Stopwatch]::StartNew()
    [void]$process.Start()
    # Read both redirected streams concurrently. Reading stdout to completion
    # before stderr can deadlock a verbose PF_DEBUG_MATCHER run when stderr's
    # pipe fills before the child has a chance to exit.
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    # Cover startup too: the application's own analysis timer cannot fire if
    # runtime/platform initialization fails before its event loop begins.
    $exited = $process.WaitForExit(($TimeoutSec + 45) * 1000)
    if (-not $exited) {
        $process.Kill()
    }
    $process.WaitForExit()
    $process.Refresh()
    $processClock.Stop()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    Write-Host $stdout
    if ($stderr) { Write-Warning $stderr }
    if (-not $exited) {
        $process.Dispose()
        throw "Analysis process exceeded the $TimeoutSec second limit and startup allowance."
    }
    if ($process.ExitCode -ne 0) {
        throw "Analysis smoke failed for mode '$mode' (exit $($process.ExitCode))"
    }
    if ($ExpectedOffsetSec -ge 0 -or $ReportPath) {
        $jsonLine = ($stdout -split '\r?\n' | Where-Object { $_ -match '^\s*results_json :' } | Select-Object -First 1)
        if (-not $jsonLine) { throw 'Missing results JSON; rebuild the executable.' }
        $pairs = @((($jsonLine -replace '^\s*results_json :\s*', '') | ConvertFrom-Json))
        if ($ReportPath) {
            $elapsedMatch = [regex]::Match($stdout, '(?m)^\s*elapsed_ms\s*:\s*(\d+)')
            $framesMatch = [regex]::Match($stdout, '(?m)^\s*frames\s*:\s*(\d+)')
            $cpuTimeMs = $null
            try { $cpuTimeMs = [math]::Round($process.TotalProcessorTime.TotalMilliseconds, 1) } catch {}
            $report = @{ video=$Video; inputVideos=@($Video) + $AdditionalVideos; mode=$mode; results=$pairs;
                debugFlags=@{analysis=$psi.EnvironmentVariables['PF_DEBUG_ANALYSIS']; matcher=$psi.EnvironmentVariables['PF_DEBUG_MATCHER']; pose=$psi.EnvironmentVariables['PF_DEBUG_POSE']};
                processWallMs=$processClock.ElapsedMilliseconds;
                cpuTimeMs=$cpuTimeMs;
                elapsedMs=$(if ($elapsedMatch.Success) { [long]$elapsedMatch.Groups[1].Value } else { $null });
                frameCount=$(if ($framesMatch.Success) { [long]$framesMatch.Groups[1].Value } else { $null });
                decodeDiagnostics=@([regex]::Matches($stderr, 'PF_DECODE [^\r\n]+') | ForEach-Object { $_.Value });
                timingDiagnostics=@([regex]::Matches($stderr, 'PF_DEBUG_TIMING [^\r\n]+') | ForEach-Object { $_.Value });
                progressDiagnostics=@([regex]::Matches($stderr, 'PF_DEBUG_PROGRESS [^\r\n]+') | ForEach-Object { $_.Value });
                inferenceDiagnostics=@([regex]::Matches($stderr, 'PF_DEBUG_INFERENCE [^\r\n]+') | ForEach-Object { $_.Value });
                previewDiagnostics=@([regex]::Matches($stderr, 'PF_DEBUG_PREVIEW [^\r\n]+') | ForEach-Object { $_.Value }) }
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

Write-Host "`nCompleted. Compare files/frames/pairs and elapsed time manually; no precision claim is made without labelled ground truth."
