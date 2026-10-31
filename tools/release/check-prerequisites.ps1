param(
    [string]$Toolchain = 'D:\msys2\ucrt64',
    [string]$ModelsDirectory = 'D:\PF_CUDA\models',
    [string]$InnoSetup = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    [string]$ReleaseNotes = 'docs/v0.2.0-release-notes.md',
    [string]$SigningKey = (Join-Path $env:LOCALAPPDATA 'ParallelFinder/release-signing/update-ed25519-private.pem')
)
# Read-only preflight: no builds, downloads, signing, settings changes or publication.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$checks = [Collections.Generic.List[object]]::new()
function Add-FileCheck([string]$Name, [string]$Path) {
    $checks.Add([pscustomobject]@{ name=$Name; available=(Test-Path -LiteralPath $Path -PathType Leaf) })
}
foreach ($name in @('cmake.exe','ninja.exe','g++.exe','objdump.exe','windeployqt.exe')) {
    Add-FileCheck "tool:$name" (Join-Path $Toolchain "bin/$name")
}
Add-FileCheck 'installer compiler' $InnoSetup
Add-FileCheck 'update signing key exists (contents not read)' $SigningKey
Add-FileCheck 'release notes' (Join-Path $root $ReleaseNotes)
foreach ($name in @('person-reid-osnet.onnx','face_detection_yunet_2023mar.onnx','face_recognition_sface_2021dec.onnx','face_detection_yunet.LICENSE','face_recognition_sface.LICENSE')) {
    Add-FileCheck "identity asset:$name" (Join-Path $ModelsDirectory $name)
}
Add-FileCheck 'release pose model' (Join-Path $root 'release-models/yolo26m-pose.onnx')
foreach ($stem in @('head-pose-6drepnet360','ava-slowfast-r50')) {
    $metadata = Join-Path $root "models/$stem.json"
    Add-FileCheck "metadata:$stem" $metadata
    if (!(Test-Path -LiteralPath $metadata -PathType Leaf)) { continue }
    try {
        $model = Get-Content -LiteralPath $metadata -Raw -Encoding UTF8 | ConvertFrom-Json
        # Metadata must name a file in models/, not an arbitrary external path.
        if ([IO.Path]::GetFileName($model.filename) -cne $model.filename -or
            [IO.Path]::GetFileName($model.licenseFile) -cne $model.licenseFile) { throw 'Invalid asset filename' }
        $path = Join-Path $root "models/$($model.filename)"
        $valid = (Test-Path -LiteralPath $path -PathType Leaf) -and
            (Get-Item -LiteralPath $path).Length -eq $model.bytes
        if ($valid) { $valid = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ieq $model.sha256 }
        $checks.Add([pscustomobject]@{ name="verified model:$stem"; available=[bool]$valid })
        Add-FileCheck "license:$stem" (Join-Path $root "models/$($model.licenseFile)")
    } catch {
        $checks.Add([pscustomobject]@{ name="valid metadata:$stem"; available=$false })
    }
}
$missing = @($checks | Where-Object { !$_.available })
[pscustomobject]@{
    scope='Build prerequisites only; this does not prove release readiness or license compliance.'
    available=($missing.Count -eq 0)
    missingCount=$missing.Count
    checks=$checks.ToArray()
} | ConvertTo-Json -Depth 4
if ($missing.Count) { exit 1 }
