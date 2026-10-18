param(
    [Parameter(Mandatory)][string]$Stage,
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$SigningKey,
    [string]$OpenSSL = 'D:/msys2/ucrt64/bin/openssl.exe',
    [string]$ChangelogPath
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
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5 -Compress), [Text.UTF8Encoding]::new($false))
& $OpenSSL pkeyutl -sign -rawin -inkey $SigningKey -in $manifestPath -out $signaturePath
if ($LASTEXITCODE) { throw 'Update signing failed' }
& $OpenSSL pkeyutl -verify -rawin -pubin -inkey $publicKey -in $manifestPath -sigfile $signaturePath
if ($LASTEXITCODE) { throw 'Signing key does not match the public key pinned in the application' }
Write-Output "SIGNED_MANIFEST=$manifestPath"
Write-Output "SIGNATURE=$signaturePath"
