param(
    [string]$Toolchain = 'D:\msys2\ucrt64',
    [string]$ModelsDirectory = 'D:\PF_CUDA\models',
    [string]$InnoSetup = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$bin = Join-Path $Toolchain 'bin'
$env:PATH = "$bin;$env:PATH"
Push-Location $root
try {
    & cmake --build --preset ucrt64-release -j 4
    if ($LASTEXITCODE) { throw 'Build failed' }
    $env:QT_QPA_PLATFORM = 'offscreen'
    & ctest --preset ucrt64-release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed: refusing to package' }
    Remove-Item Env:QT_QPA_PLATFORM
    $tag = '0.1.0-rc.3'
    $run = [guid]::NewGuid().ToString('N').Substring(0,8)
    $stage = Join-Path $root "build/package-$run/ParallelFinder"
    $out = Join-Path $root "release/$tag-$run"
    New-Item -ItemType Directory -Path $stage,$out | Out-Null
    Copy-Item -LiteralPath 'build/ucrt64-release/ParallelFinder.exe' -Destination $stage
    Copy-Item -LiteralPath LICENSE,README.md,AUDIT.md -Destination $stage
    Copy-Item -LiteralPath "$root/docs" -Destination (Join-Path $stage "docs") -Recurse
    & "$bin/windeployqt.exe" --release --no-translations --qmldir "$root/ui/qml" --dir $stage "$stage/ParallelFinder.exe"
    if ($LASTEXITCODE) { throw 'Qt deployment failed' }
    Copy-Item -LiteralPath "$PSScriptRoot/qt.conf" -Destination $stage
    Copy-Item -LiteralPath "$bin/ffmpeg.exe","$bin/onnxruntime.dll" -Destination $stage
    # Follow native PE imports rather than copying an entire developer toolchain.
    $queue = [Collections.Generic.Queue[string]]::new()
    Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object Extension -in '.dll','.exe' | ForEach-Object { $queue.Enqueue($_.FullName) }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($queue.Count) {
        $file = $queue.Dequeue()
        if (!$seen.Add($file)) { continue }
        $imports = & "$bin/objdump.exe" -p $file 2>$null | Select-String 'DLL Name:\s*(.+)$'
        foreach ($line in $imports) {
            $name = $line.Matches[0].Groups[1].Value.Trim()
            $dependency = Join-Path $bin $name
            $target = Join-Path $stage $name
            if ((Test-Path -LiteralPath $dependency) -and !(Test-Path -LiteralPath $target)) {
                Copy-Item -LiteralPath $dependency -Destination $target
                $queue.Enqueue($target)
            }
        }
    }
    $modelStage = Join-Path $stage 'models'
    New-Item -ItemType Directory -Path $modelStage | Out-Null
    foreach ($name in @('person-reid-osnet.onnx','face_detection_yunet_2023mar.onnx','face_recognition_sface_2021dec.onnx','face_detection_yunet.LICENSE','face_recognition_sface.LICENSE')) {
        Copy-Item -LiteralPath (Join-Path $ModelsDirectory $name) -Destination $modelStage
    }
    Copy-Item -LiteralPath "$root/release-models/yolo26m-pose.onnx" -Destination $modelStage
    # Include all locally available notices; provenance/source obligations remain a release gate.
    Copy-Item -LiteralPath "$Toolchain/share/licenses" -Destination (Join-Path $stage 'third-party-licenses') -Recurse
    Copy-Item -LiteralPath "$root/ui/qml/fonts/OFL.txt" -Destination (Join-Path $stage 'third-party-licenses/JetBrainsMono-OFL.txt')
    Copy-Item -LiteralPath "$root/docs/release-audit.md" -Destination (Join-Path $stage 'RELEASE-NOTES.md')
    # Test real QML loading/rendering without developer import paths or DLL paths.
    $savedEnv = @{}
    $envNames = @('PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM','PF_ORT_DLL')
    foreach ($name in $envNames) { $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
    try {
        $env:PATH = "$stage;$env:SystemRoot/System32;$env:SystemRoot"
        Remove-Item Env:QML_IMPORT_PATH,Env:QML2_IMPORT_PATH,Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
        $env:QT_QPA_PLATFORM = 'windows'
        $env:PF_ORT_DLL = Join-Path $stage 'onnxruntime.dll'
        $probe = Start-Process -FilePath "$stage/ParallelFinder.exe" -ArgumentList '--pf-ui-smoke' -WorkingDirectory $out -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/ui-smoke.log" -RedirectStandardError "$out/ui-smoke-errors.log"
        if (!$probe.WaitForExit(30000)) { $probe.Kill(); throw 'Packaged UI startup timed out' }
        if ($probe.ExitCode -ne 0) { throw "Packaged UI startup failed: $($probe.ExitCode); see $out/ui-smoke-errors.log" }
    } finally {
        foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], 'Process') }
    }
    $files = Get-ChildItem -LiteralPath $stage -Recurse -File
    $manifest = foreach ($file in $files) {
        [pscustomobject]@{Path=[IO.Path]::GetRelativePath($stage,$file.FullName); Bytes=$file.Length; SHA256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $stage 'files.sha256.json') -Encoding utf8
    $zip = Join-Path $out "ParallelFinder-$tag-Portable-x64.zip"
    & tar.exe -a -cf $zip -C (Split-Path $stage) ParallelFinder
    if ($LASTEXITCODE) { throw 'ZIP creation failed' }
    & $InnoSetup "/DStageDir=$stage" "/DOutputDir=$out" "$PSScriptRoot/installer.iss"
    if ($LASTEXITCODE) { throw 'Installer compilation failed' }
    & git archive --format=zip "--output=$out/ParallelFinder-$tag-Source.zip" HEAD
    if ($LASTEXITCODE) { throw 'Source archive failed' }
    $hashes = Get-ChildItem -LiteralPath $out -File | Get-FileHash -Algorithm SHA256
    $hashes | ForEach-Object { $_.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_.Path) } |
        Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
    $hashes | Format-Table -AutoSize
    Write-Output "STAGING=$stage"
    Write-Output "ARTIFACTS=$out"
} finally { Pop-Location }
