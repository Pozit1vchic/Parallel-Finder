param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path

function Invoke-IsolatedApp([string]$Arguments) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $Executable
    $start.Arguments = $Arguments
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # These paths deliberately cannot supply a Qt platform plugin. A probe
    # and a duplicate desktop launch must never initialize a GUI at all.
    $start.EnvironmentVariables['QT_QPA_PLATFORM'] = 'pf_intentionally_unavailable'
    $start.EnvironmentVariables['QT_PLUGIN_PATH'] = ''
    $start.EnvironmentVariables['QT_QPA_PLATFORM_PLUGIN_PATH'] = ''
    $start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Process did not start' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Launch timed out: $Arguments"
        }
        $result = @{ Code=$process.ExitCode; Out=$stdout.Result; Err=$stderr.Result }
        if ($result.Code -ne 0) { throw "Launch failed ($($result.Code)): $($result.Err)" }
        return $result
    } finally { $process.Dispose() }
}

# A desktop instance lock blocks another desktop, but never its own probes.
$created = $false
$instance = New-Object System.Threading.Mutex($false, 'Local\ParallelFinder.DesktopInstance', [ref]$created)
try {
    $duplicate = Invoke-IsolatedApp ''
    if ($duplicate.Out -match 'PF_PROVIDER_JSON=') { throw 'Wrong duplicate-launch mode' }
    $probe = Invoke-IsolatedApp '--pf-provider-probe cpu'
    $line = ($probe.Out -split "`n" | Where-Object { $_ -like 'PF_PROVIDER_JSON=*' } | Select-Object -Last 1)
    if (-not $line) { throw "Missing probe JSON: $($probe.Err)" }
    $data = $line.Substring('PF_PROVIDER_JSON='.Length) | ConvertFrom-Json
    if ($data.provider -ne 'cpu' -or -not $data.available) { throw 'CPU probe did not succeed' }
    Write-Output 'PASS: duplicate desktop exits before Qt GUI; provider probe bypasses lock and needs no platform plugin.'
} finally { $instance.Dispose() }
