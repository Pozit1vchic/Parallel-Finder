param(
    [Parameter(Mandatory=$true)][string]$Before,
    [Parameter(Mandatory=$true)][string]$After,
    [double]$RequiredSpeedup = 0
)
$ErrorActionPreference = 'Stop'
$old = Get-Content -LiteralPath $Before -Raw | ConvertFrom-Json
$new = Get-Content -LiteralPath $After -Raw | ConvertFrom-Json
if ($old.video -ne $new.video -or $old.mode -ne $new.mode -or $old.frameCount -ne $new.frameCount) {
    throw 'Reports describe different inputs, modes or frame counts.'
}
function ResultSignature($report) {
    $items = @($report.results | ForEach-Object {
        $fields = [ordered]@{}
        # Session-specific preview paths are the only excluded result fields.
        # Keep order, scores, identities, classification and every cut boundary.
        foreach ($property in ($_.PSObject.Properties | Sort-Object Name)) {
            if ($property.Name -notin @('leftPreview','rightPreview')) {
                $fields[$property.Name] = $property.Value
            }
        }
        [pscustomobject]$fields
    })
    ConvertTo-Json -InputObject $items -Depth 16 -Compress
}
if ((ResultSignature $old) -cne (ResultSignature $new)) {
    throw 'Results changed. Timing alone cannot establish a quality-preserving improvement.'
}
if ($old.elapsedMs -le 0 -or $new.elapsedMs -le 0) { throw 'Missing positive elapsed times.' }
$speedup = $old.elapsedMs / $new.elapsedMs
[pscustomobject]@{
    input = $old.video
    frames = $old.frameCount
    results = @($old.results).Count
    identicalResultFields = $true
    beforeMs = $old.elapsedMs
    afterMs = $new.elapsedMs
    speedup = [Math]::Round($speedup, 4)
    requiredSpeedup = $RequiredSpeedup
    meetsRequirement = $RequiredSpeedup -le 0 -or $speedup -ge $RequiredSpeedup
} | ConvertTo-Json
if ($RequiredSpeedup -gt 0 -and $speedup -lt $RequiredSpeedup) {
    throw "Measured speedup $speedup is below the required $RequiredSpeedup."
}
