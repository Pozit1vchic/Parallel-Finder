param(
    [string]$Toolchain = 'D:\msys2\ucrt64',
    [string]$ModelsDirectory = 'D:\PF_CUDA\models',
    [string]$InnoSetup = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    [string]$ReleaseTag = '0.1.0-rc.19.2',
    [string]$GitHubTag = '',
    [string]$ReleaseNotes = 'docs/rc19.2-release-notes.md',
    [string]$SigningKey = (Join-Path $env:LOCALAPPDATA 'ParallelFinder/release-signing/update-ed25519-private.pem'),
    [switch]$KeepStaging,
    [switch]$SkipChecks
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$bin = Join-Path $Toolchain 'bin'
$releaseOriginalPath = $env:PATH
$env:PATH = "$bin;$env:PATH"
$tag = $ReleaseTag
if ($GitHubTag -and $GitHubTag.TrimStart('v') -cne $tag.TrimStart('v')) { throw 'GitHubTag must match ReleaseTag except for the optional v prefix' }
Push-Location $root
try {
    & cmake --preset ucrt64-release "-DPF_RELEASE_LABEL=$tag"
    if ($LASTEXITCODE) { throw 'Configure failed' }
    & cmake --build --preset ucrt64-release -j 4
    if ($LASTEXITCODE) { throw 'Build failed' }
    & "$PSScriptRoot/deploy-runtime.ps1" -Stage (Join-Path $root 'build/ucrt64-release') -Toolchain $Toolchain
    if (!$SkipChecks) {
    $releaseTestQpa = $env:QT_QPA_PLATFORM
    $releaseTestPlugins = $env:QT_PLUGIN_PATH
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        # The deployed qt.conf deliberately only exposes app-local plugins;
        # development tests also need the toolchain's offscreen plugin.
        $env:QT_PLUGIN_PATH = Join-Path $Toolchain 'share/qt6/plugins'
        & ctest --preset ucrt64-release --output-on-failure
        if ($LASTEXITCODE) { throw 'Tests failed: refusing to package' }
    } finally {
        $env:QT_QPA_PLATFORM = $releaseTestQpa
        $env:QT_PLUGIN_PATH = $releaseTestPlugins
    }
    }
    $run = [guid]::NewGuid().ToString('N').Substring(0,8)
    $stage = Join-Path $root "build/package-$run/ParallelFinder"
    $out = Join-Path $root "release/$tag-$run"
    New-Item -ItemType Directory -Path $stage,$out | Out-Null
    Copy-Item -LiteralPath 'build/ucrt64-release/ParallelFinder.exe' -Destination $stage
    Copy-Item -LiteralPath 'build/ucrt64-release/ParallelFinderUpdater.exe' -Destination $stage
    Copy-Item -LiteralPath LICENSE,README.md,AUDIT.md -Destination $stage
    # Package only versioned documentation; local investigations stay local.
    foreach ($relative in (& git ls-files docs)) {
        $docTarget = Join-Path $stage $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $docTarget) | Out-Null
        Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $docTarget
    }
    & "$bin/windeployqt.exe" --release --no-translations --qmldir "$root/ui/qml" --dir $stage "$stage/ParallelFinder.exe"
    if ($LASTEXITCODE) { throw 'Qt deployment failed' }
    Copy-Item -LiteralPath "$PSScriptRoot/qt.conf" -Destination $stage
    Copy-Item -LiteralPath "$bin/ffmpeg.exe","$bin/onnxruntime.dll","$bin/onnxruntime_providers_shared.dll" -Destination $stage
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
    Copy-Item -LiteralPath (Join-Path $root $ReleaseNotes) -Destination (Join-Path $stage 'RELEASE-NOTES.md')
    # Test real QML loading/rendering without developer import paths or DLL paths.
    if (!$SkipChecks) {
    $savedEnv = @{}
    $envNames = @('PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM','PF_ORT_DLL','PF_PROVIDER_ROOT','PF_DEBUG_STARTUP','PF_UI_SMOKE_WAIT_BACKEND')
    foreach ($name in $envNames) { $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
    try {
        $env:PATH = "$stage;$env:SystemRoot/System32;$env:SystemRoot"
        Remove-Item Env:QML_IMPORT_PATH,Env:QML2_IMPORT_PATH,Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
        $env:QT_QPA_PLATFORM = 'windows'
        $env:PF_ORT_DLL = Join-Path $stage 'onnxruntime.dll'
        $env:PF_PROVIDER_ROOT = $stage
        $env:PF_DEBUG_STARTUP = '1'
        $env:PF_UI_SMOKE_WAIT_BACKEND = '1'
        $smokeLogPath = Join-Path (Split-Path $stage) 'ui-smoke.log'
        $smokeErrorPath = Join-Path (Split-Path $stage) 'ui-smoke-errors.log'
        $probe = Start-Process -FilePath "$stage/ParallelFinder.exe" -ArgumentList '--pf-ui-smoke' -WorkingDirectory $out -WindowStyle Hidden -PassThru -RedirectStandardOutput $smokeLogPath -RedirectStandardError $smokeErrorPath
        if (!$probe.WaitForExit(30000)) {
            $probe.Kill()
            $probe.WaitForExit()
            throw 'Packaged UI startup timed out'
        }
        # Windows PowerShell/.NET can leave ExitCode unpopulated after only the
        # timed WaitForExit(Int32) call, especially with redirected streams.
        # Complete the wait and refresh the Process object before reading it.
        $probe.WaitForExit()
        $probe.Refresh()
        $probeExitCode = $probe.ExitCode
        if ($null -eq $probeExitCode) {
            $details = if (Test-Path -LiteralPath $smokeErrorPath) { Get-Content -LiteralPath $smokeErrorPath -Raw } else { '' }
            throw "Packaged UI startup finished without an exit code. $details"
        }
        if ($probeExitCode -ne 0) {
            $details = if (Test-Path -LiteralPath $smokeErrorPath) { Get-Content -LiteralPath $smokeErrorPath -Raw } else { '' }
            throw "Packaged UI startup failed: $probeExitCode. $details"
        }
    } finally {
        foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], 'Process') }
    }
    }
    if ($SkipChecks) {
        'Tests and packaged startup checks were skipped at the user request. This package is not a final quality sign-off.' |
            Set-Content -LiteralPath (Join-Path $out 'CHECKS-SKIPPED.txt') -Encoding utf8
    }
    $files = Get-ChildItem -LiteralPath $stage -Recurse -File
    $manifest = foreach ($file in $files) {
        [pscustomobject]@{Path=[IO.Path]::GetRelativePath($stage,$file.FullName); Bytes=$file.Length; SHA256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $stage 'files.sha256.json') -Encoding utf8
    $zip = Join-Path $out "ParallelFinder-$tag-Portable-x64.zip"
    # The RC17.1 updater admits only directories implied by signed files.
    # windeployqt creates empty directories, so archive files explicitly.
    $archiveFiles = Join-Path (Split-Path $stage) 'archive-files.txt'
    $paths = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
        'ParallelFinder/' + [IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\','/')
    })
    [IO.File]::WriteAllLines($archiveFiles, $paths, [Text.UTF8Encoding]::new($false))
    & tar.exe -a -cf $zip -C (Split-Path $stage) -T $archiveFiles
    if ($LASTEXITCODE) { throw 'ZIP creation failed' }
    if (Test-Path -LiteralPath $SigningKey) {
        & "$PSScriptRoot/sign-update.ps1" -Stage $stage -Package $zip -Version $(if ($GitHubTag) { $GitHubTag } else { $tag }) -SigningKey $SigningKey -OpenSSL "$bin/openssl.exe" -ChangelogPath "$stage/RELEASE-NOTES.md"
    } else {
        throw 'Release signing key missing. Refusing to create an update-capable release without a trusted signature.'
    }
    if (!$SkipChecks) {
        $verification = Join-Path $out 'verification'
        New-Item -ItemType Directory -Force -Path $verification | Out-Null
        $previousPackageDirectory = $env:PF_UPDATE_PACKAGE_DIRECTORY
        try {
            $env:PF_UPDATE_PACKAGE_DIRECTORY = $out
            & (Join-Path $root 'build/ucrt64-release/pf_update_tests.exe') signedReleaseArchiveInstalls -o "$(Join-Path $verification 'archive-install-test.txt'),txt"
            if ($LASTEXITCODE) { throw 'Signed release archive failed updater extraction/health verification' }
        } finally { $env:PF_UPDATE_PACKAGE_DIRECTORY = $previousPackageDirectory }
    }
    & $InnoSetup "/DStageDir=$stage" "/DOutputDir=$out" "/DAppVersion=$tag" "$PSScriptRoot/installer.iss"
    if ($LASTEXITCODE) { throw 'Installer compilation failed' }
    # Archive tracked working-tree sources only. Read their current contents
    # (rather than `git archive HEAD`) so staged/uncommitted fixes are included,
    # but unrelated untracked folders can never leak into a release archive.
    $sourceStage = Join-Path $root "build/source-$run/ParallelFinder-$tag-Source"
    New-Item -ItemType Directory -Path $sourceStage | Out-Null
    $trackedFiles = & git ls-files --cached
    if ($LASTEXITCODE) { throw 'git ls-files failed' }
    foreach ($relative in $trackedFiles) {
        if ([string]::IsNullOrWhiteSpace($relative)) { continue }
        $source = Join-Path $root $relative
        if (!(Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        $target = Join-Path $sourceStage $relative
        $targetDir = Split-Path -Parent $target
        if ($targetDir) { New-Item -ItemType Directory -Force -Path $targetDir | Out-Null }
        Copy-Item -LiteralPath $source -Destination $target
    }
    $sourceZip = Join-Path $out "ParallelFinder-$tag-Source.zip"
    & tar.exe -a -cf $sourceZip -C (Split-Path $sourceStage) (Split-Path $sourceStage -Leaf)
    if ($LASTEXITCODE) { throw 'Source archive failed' }
    $hashes = Get-ChildItem -LiteralPath $out -File | Get-FileHash -Algorithm SHA256
    $hashes | ForEach-Object { $_.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_.Path) } |
        Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
    $hashes | Format-Table -AutoSize
    if ($KeepStaging) { Write-Output "STAGING=$stage" }
    else {
        # Clean this successful run only, after every deliverable is complete.
        $buildRoot = [IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar
        foreach ($temporary in @((Split-Path $stage), (Split-Path $sourceStage))) {
            $resolved = (Resolve-Path -LiteralPath $temporary).Path
            if (!$resolved.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to remove staging outside build: $resolved"
            }
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
    Write-Output "ARTIFACTS=$out"
} finally {
    $env:PATH = $releaseOriginalPath
    Pop-Location
}
