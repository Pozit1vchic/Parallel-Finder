param(
    [string]$Exe = 'D:\Parallel-Finder\build\ucrt64-release\ParallelFinder.exe',
    [ValidateRange(1, 10)][int]$Repeats = 3,
    [string]$ReportPath = '',
    [switch]$ProfileStartupUI,
    [switch]$LocalProbeBaseline
)
$ErrorActionPreference = 'Stop'
$records = @()
for ($iteration = 1; $iteration -le $Repeats; ++$iteration) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = (Resolve-Path -LiteralPath $Exe).Path
    $psi.Arguments = if ($ProfileStartupUI) { '--pf-startup-profile' } else { '--pf-ui-smoke' }
    if ($ProfileStartupUI -and $LocalProbeBaseline) { $psi.Arguments += ' --pf-profile-local-probe' }
    $psi.UseShellExecute = $false
    $psi.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.EnvironmentVariables['QT_QPA_PLATFORM'] = 'windows'
    $psi.EnvironmentVariables['PF_DEBUG_STARTUP'] = '1'
    $psi.EnvironmentVariables['PF_UI_SMOKE_WAIT_BACKEND'] = '1'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $psi
    try {
        $timer = [Diagnostics.Stopwatch]::StartNew()
        [void]$process.Start()
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'Startup smoke timed out'
        }
        $process.WaitForExit()
        $timer.Stop()
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0 -or $stdout -notmatch $(if ($ProfileStartupUI) { 'PF_STARTUP_UI' } else { 'window rendered' })) {
            throw "Startup smoke failed (exit $($process.ExitCode)): $stdout $stderr"
        }
        $first = [regex]::Match($stderr, 'PF_STARTUP first_frame_ms=(\d+) backend_pending=(\d+)')
        $ready = [regex]::Match($stderr, 'PF_STARTUP backend_ready_ms=(\d+)')
        $ui = [regex]::Match($stdout, 'PF_STARTUP_UI first_frame_ms=(\d+) frames=(\d+) max_active_gap_ms=(\d+) max_heartbeat_ms=(\d+) settled=(\d+)')
        $motion = [regex]::Match($stdout, 'PF_MOTION changed_frames=(\d+) sampled_frames=(\d+)')
        $preparation = [regex]::Match($stdout, 'preparation_gap_ms=(\d+)')
        $guiRuntime = [regex]::Match($stdout, 'PF_STARTUP_GUI_ORT_LOADED=(\d+)')
        $records += [pscustomobject]@{
            iteration = $iteration
            localProbeBaseline = [bool]$LocalProbeBaseline
            changedMotionFrames = $(if ($motion.Success) { [long]$motion.Groups[1].Value } else { $null })
            sampledMotionFrames = $(if ($motion.Success) { [long]$motion.Groups[2].Value } else { $null })
            preparationGapMs = $(if ($preparation.Success) { [long]$preparation.Groups[1].Value } else { $null })
            guiOrtLoaded = $(if ($guiRuntime.Success) { $guiRuntime.Groups[1].Value -eq '1' } else { $null })
            frames = $(if ($ui.Success) { [long]$ui.Groups[2].Value } else { $null })
            maxActiveFrameGapMs = $(if ($ui.Success) { [long]$ui.Groups[3].Value } else { $null })
            maxUiHeartbeatGapMs = $(if ($ui.Success) { [long]$ui.Groups[4].Value } else { $null })
            dialogSettled = $(if ($ui.Success) { $ui.Groups[5].Value -eq '1' } else { $null })
            # Main-entry to presented frame excludes OS DLL loading; do not
            # compare this directly with the process-lifetime measurement.
            firstFrameMs = $(if ($first.Success) { [long]$first.Groups[1].Value } else { $null })
            backendPendingAtFirstFrame = $(if ($first.Success) { $first.Groups[2].Value -eq '1' } else { $null })
            backendReadyMs = $(if ($ready.Success) { [long]$ready.Groups[1].Value } else { $null })
            totalProcessMs = $timer.ElapsedMilliseconds
        }
    } finally { $process.Dispose() }
}
if ($ReportPath) { $records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ReportPath -Encoding UTF8 }
$records
