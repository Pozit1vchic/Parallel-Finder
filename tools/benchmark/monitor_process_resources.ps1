param(
    [Parameter(Mandatory=$true)][int]$TargetProcessId,
    [Parameter(Mandatory=$true)][string]$OutputPath
)
$ErrorActionPreference='Stop'
# Windows WDDM exposes per-process dedicated/shared GPU allocations even when
# nvidia-smi cannot report per-process memory. Keep raw samples, not a claimed
# device-wide or instantaneous peak. The analysis process never waits on CIM.
$writer=[IO.StreamWriter]::new($OutputPath,$false,[Text.UTF8Encoding]::new($false))
$writer.AutoFlush=$true
try {
    while($true) {
        $process=Get-CimInstance Win32_Process -Filter "ProcessId=$TargetProcessId"
        if(-not $process){break}
        $sample=@{utc=[DateTime]::UtcNow.ToString('O');processId=$TargetProcessId
            workingSetBytes=[long]$process.WorkingSetSize;peakWorkingSetKiB=[long]$process.PeakWorkingSetSize
            privateBytes=[long]$process.PrivatePageCount
            cpu100ns=([long]$process.KernelModeTime+[long]$process.UserModeTime)
            ioReadBytes=[long]$process.ReadTransferCount;ioWriteBytes=[long]$process.WriteTransferCount}
        try {
            $gpuMemory=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory |
                Where-Object Name -Like "pid_${TargetProcessId}_*")
            $sample.gpuMemory=@($gpuMemory | ForEach-Object {@{instance=$_.Name;dedicatedBytes=[long]$_.DedicatedUsage;sharedBytes=[long]$_.SharedUsage;committedBytes=[long]$_.TotalCommitted}})
            $engines=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine |
                Where-Object Name -Like "pid_${TargetProcessId}_*")
            $sample.gpuEngines=@($engines | ForEach-Object {
                $reported=[double]$_.UtilizationPercentage
                $valid=[double]::IsFinite($reported) -and $reported -ge 0 -and $reported -le 100
                @{instance=$_.Name;reportedUtilizationPercent=$reported;utilizationValid=$valid;
                    utilizationPercent=$(if($valid){$reported}else{$null})}
            })
        } catch {$sample.gpuError=$_.Exception.Message}
        $writer.WriteLine(($sample | ConvertTo-Json -Depth 5 -Compress))
        Start-Sleep -Milliseconds 1000
    }
} finally {$writer.Dispose()}
