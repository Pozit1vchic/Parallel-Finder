param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [string]$Toolchain = 'D:\msys2\ucrt64'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$stagePath = (Resolve-Path -LiteralPath $Stage).Path
$bin = Join-Path $Toolchain 'bin'
& "$bin/windeployqt.exe" --release --no-translations --qmldir "$root/ui/qml" --dir $stagePath "$stagePath/ParallelFinder.exe"
if ($LASTEXITCODE) { throw 'Development runtime deployment failed' }
Copy-Item -LiteralPath "$PSScriptRoot/qt.conf" -Destination $stagePath
foreach ($name in @('onnxruntime.dll', 'onnxruntime_providers_shared.dll')) {
    Copy-Item -LiteralPath (Join-Path $bin $name) -Destination $stagePath
}
# Follow actual PE imports. This also provides FFmpeg/OpenSSL dependencies
# for isolated startup tests without exposing the whole toolchain on PATH.
$queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $stagePath -File | Where-Object Extension -in '.dll','.exe' |
    ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while ($queue.Count) {
    $file = $queue.Dequeue()
    if (!$seen.Add($file)) { continue }
    foreach ($line in (& "$bin/objdump.exe" -p $file 2>$null | Select-String 'DLL Name:\s*(.+)$')) {
        $name = $line.Matches[0].Groups[1].Value.Trim()
        $dependency = Join-Path $bin $name
        $target = Join-Path $stagePath $name
        if ((Test-Path -LiteralPath $dependency) -and !(Test-Path -LiteralPath $target)) {
            Copy-Item -LiteralPath $dependency -Destination $target
            $queue.Enqueue($target)
        }
    }
}
