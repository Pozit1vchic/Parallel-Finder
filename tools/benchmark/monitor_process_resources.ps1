param(
    [Parameter(Mandatory=$true)][int]$TargetProcessId,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [ValidateRange(0,1000000)][int]$MaxSamples=0
)
$ErrorActionPreference='Stop'
# Windows WDDM exposes per-process dedicated/shared GPU allocations even when
# nvidia-smi cannot report per-process memory. Keep raw samples, not a claimed
# device-wide or instantaneous peak. The analysis process never waits on CIM.
$writer=[IO.StreamWriter]::new($OutputPath,$false,[Text.UTF8Encoding]::new($false))
$writer.AutoFlush=$true
$samples=0
$reportVolume=[IO.DriveInfo]::new([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($OutputPath)))
try {
    while($true) {
        $process=Get-CimInstance Win32_Process -Filter "ProcessId=$TargetProcessId"
        if(-not $process){break}
        $sample=@{utc=[DateTime]::UtcNow.ToString('O');processId=$TargetProcessId
            workingSetBytes=[long]$process.WorkingSetSize;peakWorkingSetKiB=[long]$process.PeakWorkingSetSize
            privateBytes=[long]$process.PrivatePageCount
            cpu100ns=([long]$process.KernelModeTime+[long]$process.UserModeTime)
            ioReadBytes=[long]$process.ReadTransferCount;ioWriteBytes=[long]$process.WriteTransferCount}
        try {$sample.reportVolumeFreeBytes=$reportVolume.AvailableFreeSpace}
        catch {$sample.diskError=$_.Exception.Message}
        try {
            $gpuMemory=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory |
                Where-Object Name -Like "pid_${TargetProcessId}_*")
            $sample.gpuMemory=@($gpuMemory | ForEach-Object {@{instance=$_.Name;dedicatedBytes=[long]$_.DedicatedUsage;sharedBytes=[long]$_.SharedUsage;committedBytes=[long]$_.TotalCommitted}})
            $engines=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine |
                Where-Object Name -Like "pid_${TargetProcessId}_*")
            $sample.gpuEngines=@($engines | ForEach-Object {
                $reported=[double]$_.UtilizationPercentage
                # Windows PowerShell 5.1 uses .NET Framework, which has no
                # Double.IsFinite. Preserve the same validity rule there.
                $valid=-not [double]::IsNaN($reported) -and -not [double]::IsInfinity($reported) -and $reported -ge 0 -and $reported -le 100
                @{instance=$_.Name;reportedUtilizationPercent=$reported;utilizationValid=$valid;
                    utilizationPercent=$(if($valid){$reported}else{$null})}
            })
        } catch {$sample.gpuError=$_.Exception.Message}
        $writer.WriteLine(($sample | ConvertTo-Json -Depth 5 -Compress))
        ++$samples
        if($MaxSamples -gt 0 -and $samples -ge $MaxSamples){break}
        Start-Sleep -Milliseconds 1000
    }
} finally {$writer.Dispose()}
