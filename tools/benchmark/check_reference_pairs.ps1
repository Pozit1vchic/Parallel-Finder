param(
    [Parameter(Mandatory = $true)][string]$ReportPath,
    [string]$ReferencePath = "$PSScriptRoot/soldier-reference.json"
)
$ErrorActionPreference = 'Stop'
$report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
$reference = Get-Content -LiteralPath $ReferencePath -Raw | ConvertFrom-Json
$tolerance = [double]$reference.anchorToleranceSeconds
$hits = 0
foreach ($anchor in $reference.positiveAnchorsSeconds) {
    $a = [double]$anchor[0]; $b = [double]$anchor[1]
    $found = @($report.results | Where-Object {
        ($_.leftStart -le $a+$tolerance -and $_.leftEnd -ge $a-$tolerance -and
         $_.rightStart -le $b+$tolerance -and $_.rightEnd -ge $b-$tolerance) -or
        ($_.rightStart -le $a+$tolerance -and $_.rightEnd -ge $a-$tolerance -and
         $_.leftStart -le $b+$tolerance -and $_.leftEnd -ge $b-$tolerance)
    })
    if ($found.Count) { ++$hits; Write-Host "FOUND $a <-> $b" }
    else { Write-Host "MISSING $a <-> $b" }
}
Write-Host "$hits / $($reference.positiveAnchorsSeconds.Count) labelled anchors retrieved. This does not measure precision."
$falseHits = 0
foreach ($anchor in $reference.negativeAnchorsSeconds) {
    $a = [double]$anchor[0]; $b = [double]$anchor[1]
    $found = @($report.results | Where-Object {
        ($_.leftStart -le $a+$tolerance -and $_.leftEnd -ge $a-$tolerance -and
         $_.rightStart -le $b+$tolerance -and $_.rightEnd -ge $b-$tolerance) -or
        ($_.rightStart -le $a+$tolerance -and $_.rightEnd -ge $a-$tolerance -and
         $_.leftStart -le $b+$tolerance -and $_.leftEnd -ge $b-$tolerance)
    })
    if ($found.Count) { ++$falseHits; Write-Host "FALSE MATCH $a <-> $b" }
    else { Write-Host "REJECTED negative $a <-> $b" }
}
if ($falseHits) { throw "$falseHits labelled negative pairs were accepted." }
if ($hits -ne $reference.positiveAnchorsSeconds.Count) { throw 'Reference-pair regression is incomplete.' }
