param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$profile = Join-Path ([IO.Path]::GetTempPath()) ('pf-healthcheck-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $profile | Out-Null
try {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $Executable
    $start.Arguments = '--pf-update-healthcheck'
    $start.WorkingDirectory = Split-Path -Parent $Executable
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.EnvironmentVariables['APPDATA'] = $profile
    $start.EnvironmentVariables['LOCALAPPDATA'] = $profile
    $start.EnvironmentVariables['QT_QPA_PLATFORM'] = 'offscreen'
    $start.EnvironmentVariables['QT_PLUGIN_PATH'] = ''
    $start.EnvironmentVariables['QT_QPA_PLATFORM_PLUGIN_PATH'] = ''
    $start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Process did not start' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'Update health check timed out'
        }
        if ($process.ExitCode -ne 0 -or $stdout.Result -notmatch 'UI smoke: window rendered') {
            throw "Update health check failed ($($process.ExitCode)): $($stderr.Result)"
        }
        Write-Output 'PASS: packaged update health check renders with inherited offscreen setting and empty profile.'
    } finally { $process.Dispose() }
} finally {
    $resolved = (Resolve-Path -LiteralPath $profile).Path
    $prefix = Join-Path ([IO.Path]::GetFullPath([IO.Path]::GetTempPath())) 'pf-healthcheck-'
    if ($resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
