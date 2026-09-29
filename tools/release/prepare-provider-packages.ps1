#requires -Version 7.0
param(
    [string]$ProvidersRoot = "$env:LOCALAPPDATA/ParallelFinder/ParallelFinder/providers",
    [string]$LibrariesRoot = 'D:/CUDA_Libraries',
    [string]$OutputRoot = '',
    [string[]]$Providers = @('cuda', 'tensorrt', 'dml')
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$OutputRoot) { $OutputRoot = Join-Path $repo ('release/runtime-input-' + [guid]::NewGuid().ToString('N').Substring(0,8)) }
if (Test-Path -LiteralPath $OutputRoot) { throw "Refusing to overwrite existing folder: $OutputRoot" }
$common = @('onnxruntime.dll','onnxruntime_providers_shared.dll','onnxruntime_providers_cuda.dll',
    'cublas64_12.dll','cublasLt64_12.dll','cudart64_12.dll','cufft64_11.dll',
    'cudnn64_9.dll','cudnn_adv64_9.dll','cudnn_cnn64_9.dll','cudnn_graph64_9.dll',
    'cudnn_ops64_9.dll','cudnn_heuristic64_9.dll','cudnn_engines_precompiled64_9.dll',
    'cudnn_engines_runtime_compiled64_9.dll')
$trt = @('onnxruntime_providers_tensorrt.dll','nvinfer_10.dll','nvinfer_builder_resource_10.dll',
    'nvinfer_dispatch_10.dll','nvinfer_lean_10.dll','nvinfer_plugin_10.dll',
    'nvinfer_vc_plugin_10.dll','nvonnxparser_10.dll')
# Plan and validate all inputs before copying; never mix different ORT builds.
$plan = @()
foreach ($provider in $Providers) {
    if ($provider -notin @('cuda','tensorrt','dml')) { throw "Unsupported provider: $provider" }
    $source = Join-Path $ProvidersRoot $provider
    $names = $common + $(if ($provider -eq 'tensorrt') { $trt } else { @() })
    if ($provider -eq 'dml') { $names = @('onnxruntime.dll','onnxruntime_providers_shared.dll','DirectML.dll') }
    foreach ($name in $names) {
        $path = Join-Path $source $name
        # Only the explicitly pinned cuDNN/TRT distributions are fallback sources.
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            if ($name -like 'cudnn*') { $path = Join-Path $LibrariesRoot "cudnn-windows-x86_64-9.10.0.56_cuda12/bin/$name" }
            elseif ($name -like 'nvinfer*' -or $name -like 'nvonnxparser*') { $path = Join-Path $LibrariesRoot "TensorRT-10.9.0.34/lib/$name" }
        }
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing dependency: $provider/$name" }
        $file = Get-Item -LiteralPath $path
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked input rejected: $path" }
        $plan += [pscustomobject]@{ provider=$provider; name=$name; source=$file.FullName; version=$file.VersionInfo.FileVersion }
    }
    $versions = @($plan | Where-Object { $_.provider -eq $provider -and $_.name -like 'onnxruntime*' } | ForEach-Object version | Select-Object -Unique)
    if ($versions.Count -ne 1 -or !$versions[0]) { throw "Mixed or unversioned ORT DLLs in $provider" }
}
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$inventory = @()
foreach ($entry in $plan) {
    $destination = Join-Path $OutputRoot $entry.provider
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $target = Join-Path $destination $entry.name
    Copy-Item -LiteralPath $entry.source -Destination $target
    $inventory += [pscustomobject]@{ provider=$entry.provider; file=$entry.name; version=$entry.version;
        bytes=(Get-Item -LiteralPath $target).Length; sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
}
foreach ($provider in $Providers) {
    $licenses = Join-Path $OutputRoot "$provider/licenses"
    New-Item -ItemType Directory -Path $licenses | Out-Null
    $cudnnLicense = Join-Path $LibrariesRoot 'cudnn-windows-x86_64-9.10.0.56_cuda12/LICENSE'
    if ($provider -ne 'dml' -and (Test-Path -LiteralPath $cudnnLicense)) { Copy-Item -LiteralPath $cudnnLicense -Destination (Join-Path $licenses 'cuDNN-LICENSE.txt') }
    Get-ChildItem -LiteralPath (Join-Path $ProvidersRoot $provider) -File |
        Where-Object { $_.Name -match 'LICENSE|NOTICE|EULA' } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $licenses }
}
$inventory | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputRoot 'inventory.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $repo 'docs/provider-packages-howto.md') -Destination (Join-Path $OutputRoot 'HOW-TO.md')
Write-Output "INPUT_ROOT=$((Resolve-Path -LiteralPath $OutputRoot).Path)"
Write-Output 'Local preparation only. Add version-matched licenses/notices and validate before publication.'
