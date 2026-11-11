param(
    [Parameter(Mandatory)][string]$Stage,
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$SigningKey,
    [string]$OpenSSL = 'D:/msys2/ucrt64/bin/openssl.exe',
    [string]$ChangelogPath,
    [string]$PreviousPackage,
    [string]$PreviousVersion
)
$ErrorActionPreference = 'Stop'
function Get-ReleaseRelativePath([string]$Base, [string]$Path) {
    # Windows PowerShell 5.1 runs on .NET Framework, which has no
    # Path.GetRelativePath; use URI-relative paths for the signed manifest.
    $baseFull = [IO.Path]::GetFullPath($Base).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $pathFull = [IO.Path]::GetFullPath($Path)
    $baseUri = [Uri]::new($baseFull)
    $pathUri = [Uri]::new($pathFull)
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', [IO.Path]::DirectorySeparatorChar)
}
$outputDirectory = Split-Path -Parent $Package
$manifestPath = Join-Path $outputDirectory 'update.json'
$signaturePath = Join-Path $outputDirectory 'update.json.sig'
$publicKey = Join-Path $PSScriptRoot 'update-public-key.pem'
$files = @(Get-ChildItem -LiteralPath $Stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = (Get-ReleaseRelativePath $Stage $_.FullName).Replace([IO.Path]::DirectorySeparatorChar, '/')
        size = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$changelog = if ($ChangelogPath) { [IO.File]::ReadAllText((Resolve-Path -LiteralPath $ChangelogPath).Path, [Text.Encoding]::UTF8) } else { '' }
if ($changelog.Length -gt 5000) { $changelog = $changelog.Substring(0,5000) }
$manifest = [ordered]@{
    schema = 1
    repository = 'Pozit1vchic/Parallel-Finder'
    platform = 'windows-x64'
    version = $Version
    channel = if ($Version.Contains('-')) { 'beta' } else { 'stable' }
    asset = [IO.Path]::GetFileName($Package)
    size = (Get-Item -LiteralPath $Package).Length
    sha256 = (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash.ToLowerInvariant()
    changelog = $changelog
    files = $files
}
if ($PreviousPackage -or $PreviousVersion) {
    if (!$PreviousPackage -or !$PreviousVersion) { throw 'PreviousPackage and PreviousVersion must be supplied together' }
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $previous = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $PreviousPackage).Path)
    $reuse = [Collections.Generic.List[string]]::new()
    try {
        foreach ($file in $files) {
            $entry = $previous.GetEntry('ParallelFinder/' + $file.path)
            if (!$entry -or $entry.Length -ne $file.size) { continue }
            $stream = $entry.Open()
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
            finally { $hasher.Dispose(); $stream.Dispose() }
            if ($hash -eq $file.sha256) { $reuse.Add($file.path) }
        }
    } finally { $previous.Dispose() }
    if ($reuse.Count -gt 0 -and $reuse.Count -lt $files.Count) {
        $deltaName = "ParallelFinder-$Version-from-$PreviousVersion-Incremental-x64.zip"
        if ([IO.Path]::GetFileName($deltaName) -ne $deltaName -or $deltaName.Contains('\')) { throw 'Unsafe incremental version spelling' }
        $deltaPath = Join-Path $outputDirectory $deltaName
        # CreateNew prevents silently overwriting an existing release artifact.
        $output = [IO.Compression.ZipFile]::Open($deltaPath, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($file in $files) {
                if ($reuse.Contains($file.path)) { continue }
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($output, (Join-Path $Stage $file.path),
                    ('ParallelFinder/' + $file.path), [IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        } finally { $output.Dispose() }
        $deltaSize = (Get-Item -LiteralPath $deltaPath).Length
        if ($deltaSize -lt $manifest.size) {
            $manifest.incremental = @([ordered]@{
                baseVersion = $PreviousVersion; asset = $deltaName; size = $deltaSize
                sha256 = (Get-FileHash -LiteralPath $deltaPath -Algorithm SHA256).Hash.ToLowerInvariant()
                reuse = @($reuse.ToArray())
            })
            Write-Output "INCREMENTAL_PACKAGE=$deltaPath"
            Write-Output "UPDATE_BYTES full=$($manifest.size) incremental=$deltaSize reusedFiles=$($reuse.Count)"
        } else {
            # Only the newly created output is removed, never the input package.
            Remove-Item -LiteralPath $deltaPath
            Write-Output 'Incremental archive provides no bandwidth saving; retaining full package only'
        }
    }
}
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5 -Compress), [Text.UTF8Encoding]::new($false))
& $OpenSSL pkeyutl -sign -rawin -inkey $SigningKey -in $manifestPath -out $signaturePath
if ($LASTEXITCODE) { throw 'Update signing failed' }
& $OpenSSL pkeyutl -verify -rawin -pubin -inkey $publicKey -in $manifestPath -sigfile $signaturePath
if ($LASTEXITCODE) { throw 'Signing key does not match the public key pinned in the application' }
Write-Output "SIGNED_MANIFEST=$manifestPath"
Write-Output "SIGNATURE=$signaturePath"
