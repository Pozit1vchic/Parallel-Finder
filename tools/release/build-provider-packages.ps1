#requires -Version 7.0
param(
    [Parameter(Mandatory=$true)][string]$InputRoot,
    [string]$Exe = "$PSScriptRoot/../../build/ucrt64-release/ParallelFinder.exe",
    [string]$OutputRoot = '',
    [string]$ExistingArchivesRoot = '',
    [string]$Repository = 'Pozit1vchic/Parallel-Finder',
    [string]$Tag = 'runtime-v1'
)
$ErrorActionPreference = 'Stop'
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Tag -notmatch '^[A-Za-z0-9_.-]+$') { throw 'Invalid release repository/tag' }
$InputRoot = (Resolve-Path -LiteralPath $InputRoot).Path
$Exe = (Resolve-Path -LiteralPath $Exe).Path
if (!$OutputRoot) { $OutputRoot = Join-Path (Split-Path $InputRoot) ('runtime-assets-' + [guid]::NewGuid().ToString('N').Substring(0,8)) }
if (Test-Path -LiteralPath $OutputRoot) { throw "Refusing to overwrite existing output: $OutputRoot" }
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$OutputRoot = (Resolve-Path -LiteralPath $OutputRoot).Path
$assets = @()
$validation = @()
$saved = @{}
foreach ($name in @('PATH','PF_ORT_DLL','PF_PROVIDER_ROOT','PF_TRT_CACHE_PATH')) { $saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($provider in @('cuda','tensorrt','dml')) {
        $folder = Join-Path $InputRoot $provider
        if (!(Test-Path -LiteralPath $folder -PathType Container)) { continue }
        $files = @(Get-ChildItem -LiteralPath $folder -Recurse -Force)
        if ($files | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Linked package entries are not allowed' }
        $requiredDlls = @('onnxruntime.dll','onnxruntime_providers_shared.dll')
        $requiredDlls += $(if ($provider -eq 'dml') { 'DirectML.dll' } else { 'onnxruntime_providers_cuda.dll' })
        foreach ($required in $requiredDlls) {
            if (!(Test-Path -LiteralPath (Join-Path $folder $required) -PathType Leaf)) { throw "Missing $provider/$required" }
        }
        if ($provider -eq 'tensorrt' -and !(Test-Path -LiteralPath (Join-Path $folder 'onnxruntime_providers_tensorrt.dll'))) { throw 'Missing TensorRT EP' }
        $env:PF_ORT_DLL = Join-Path $folder 'onnxruntime.dll'
        $env:PF_PROVIDER_ROOT = $folder
        $env:PF_TRT_CACHE_PATH = Join-Path $OutputRoot 'probe-cache'
        $env:PATH = "$folder;$env:SystemRoot/System32;$env:SystemRoot"
        $stdout = Join-Path $OutputRoot "$provider-probe.log"
        $stderr = Join-Path $OutputRoot "$provider-probe-errors.log"
        $process = Start-Process -FilePath $Exe -ArgumentList '--pf-provider-probe',$provider -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if (!$process.WaitForExit(120000)) { $process.Kill(); throw "$provider probe timed out" }
        $line = Get-Content -LiteralPath $stdout | Where-Object { $_.StartsWith('PF_PROVIDER_JSON=') } | Select-Object -Last 1
        if ($process.ExitCode -ne 0 -or !$line) { throw "$provider probe failed; see $stderr" }
        $probe = $line.Substring('PF_PROVIDER_JSON='.Length) | ConvertFrom-Json
        if ($probe.provider -ne $provider -or !$probe.available) { throw "$provider unavailable: $($probe.reason)" }
        $requiredLicenses = @('ONNX-Runtime-LICENSE.txt','CUDA-EULA.txt','cuDNN-LICENSE.txt')
        if ($provider -eq 'dml') { $requiredLicenses = @('ONNX-Runtime-LICENSE.txt','DirectML-LICENSE.txt') }
        if ($provider -eq 'tensorrt') { $requiredLicenses += 'TensorRT-LICENSE.txt' }
        $missing = @($requiredLicenses | Where-Object { !(Test-Path -LiteralPath (Join-Path $folder "licenses/$_") -PathType Leaf) })
        $archiveName = "parallel-finder-runtime-$provider-win64.zip"
        $archive = Join-Path $OutputRoot $archiveName
        $existing = if ($ExistingArchivesRoot) { Join-Path $ExistingArchivesRoot $archiveName } else { '' }
        if ($existing -and (Test-Path -LiteralPath $existing -PathType Leaf)) {
            # Reuse only byte-for-byte verified payloads, never a stale archive
            # after the operator has added licenses or replaced DLLs.
            $zip = [IO.Compression.ZipFile]::OpenRead($existing)
            try {
                $regular = @($zip.Entries | Where-Object { $_.Name })
                $sourceFiles = @(Get-ChildItem -LiteralPath $folder -Recurse -File)
                if ($regular.Count -ne $sourceFiles.Count) { throw 'Existing archive has a different file set' }
                foreach ($file in $sourceFiles) {
                    $relative = [IO.Path]::GetRelativePath($folder,$file.FullName).Replace('\','/')
                    $entry = $zip.GetEntry($relative)
                    if (!$entry -or $entry.Length -ne $file.Length) { throw "Stale archive entry: $relative" }
                    $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
                    try { $actual = [Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant() }
                    finally { $sha.Dispose(); $stream.Dispose() }
                    if ($actual -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()) { throw "Stale archive bytes: $relative" }
                }
            } finally { $zip.Dispose() }
            Copy-Item -LiteralPath $existing -Destination $archive
        } else {
            [IO.Compression.ZipFile]::CreateFromDirectory($folder, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
        }
        $size = (Get-Item -LiteralPath $archive).Length
        $asset = [ordered]@{ provider=$provider; archive=$archiveName; sizeBytes=$size;
            sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant();
            downloadUrl="https://github.com/$Repository/releases/download/$Tag/$archiveName" }
        if ($size -ge 2GB) {
            if ($size -gt 8GB) { throw 'Complete provider archive exceeds the application 8 GiB bound' }
            $parts = @(); $inputStream = [IO.File]::OpenRead($archive)
            try {
                $buffer = [byte[]]::new(4MB); $index = 1
                while ($inputStream.Position -lt $inputStream.Length) {
                    $name = '{0}.part{1:D3}' -f $archiveName,$index
                    $partPath = Join-Path $OutputRoot $name
                    $partStream = [IO.File]::Open($partPath,[IO.FileMode]::CreateNew)
                    try {
                        [long]$remaining = 1800MB
                        while ($remaining -gt 0 -and $inputStream.Position -lt $inputStream.Length) {
                            $read = $inputStream.Read($buffer,0,[int][Math]::Min($buffer.Length,$remaining))
                            if (!$read) { throw 'Unexpected EOF while splitting archive' }
                            $partStream.Write($buffer,0,$read); $remaining -= $read
                        }
                    } finally { $partStream.Dispose() }
                    $parts += [ordered]@{ archive=$name; sizeBytes=(Get-Item -LiteralPath $partPath).Length;
                        sha256=(Get-FileHash -LiteralPath $partPath -Algorithm SHA256).Hash.ToLowerInvariant();
                        downloadUrl="https://github.com/$Repository/releases/download/$Tag/$name" }
                    ++$index
                }
            } finally { $inputStream.Dispose() }
            $asset.Remove('downloadUrl')
            $asset['parts'] = $parts
        }
        $assets += $asset
        $validation += [ordered]@{ provider=$provider; isolatedSessionProbe=$true; realModelInferenceVerified=$false;
            missingLicenseFiles=$missing; redistributionReviewCompleted=$false }
        Write-Output "PACKAGED=$archive"
    }
    if (!$assets.Count) { throw 'No provider input directories found' }
    @{ providers=$assets } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputRoot 'providers.draft.json') -Encoding utf8
    $validation | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputRoot 'validation.json') -Encoding utf8
    Write-Output "ASSET_ROOT=$OutputRoot"
    Write-Output 'Draft only: review validation.json and licenses; publish manifest LAST after ZIP uploads.'
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process') }
}
