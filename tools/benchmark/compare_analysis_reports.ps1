param(
    [Parameter(Mandatory=$true)][string]$Before,
    [Parameter(Mandatory=$true)][string]$After,
    [double]$RequiredSpeedup = 0
)
$ErrorActionPreference = 'Stop'
$old = Get-Content -LiteralPath $Before -Raw | ConvertFrom-Json
$new = Get-Content -LiteralPath $After -Raw | ConvertFrom-Json
foreach ($report in @($old,$new)) {
    foreach ($field in @('video','mode','frameCount','elapsedMs','results')) {
        if ($null -eq $report.PSObject.Properties[$field] -or $null -eq $report.$field) {
            throw "Incomplete report: missing $field."
        }
    }
    if ([string]::IsNullOrWhiteSpace($report.video) -or [string]::IsNullOrWhiteSpace($report.mode) -or
        $report.frameCount -le 0) { throw 'Invalid source, mode or frame count.' }
}
if ($old.video -ne $new.video -or $old.mode -ne $new.mode -or $old.frameCount -ne $new.frameCount) {
    throw 'Reports describe different inputs, modes or frame counts.'
}
foreach ($field in @('inputVideos','debugFlags')) {
    if ($field -eq 'debugFlags') {
        # JSON object member order does not change the diagnostic settings.
        $oldFlags = [ordered]@{}
        $newFlags = [ordered]@{}
        foreach ($p in ($old.debugFlags.PSObject.Properties | Sort-Object Name)) { $oldFlags[$p.Name] = $p.Value }
        foreach ($p in ($new.debugFlags.PSObject.Properties | Sort-Object Name)) { $newFlags[$p.Name] = $p.Value }
        if ((ConvertTo-Json $oldFlags -Compress) -cne (ConvertTo-Json $newFlags -Compress)) {
            throw 'Reports use different debugFlags; timing is not comparable.'
        }
        continue
    }
    $oldValue=ConvertTo-Json -InputObject $old.$field -Depth 16 -Compress
    $newValue=ConvertTo-Json -InputObject $new.$field -Depth 16 -Compress
    if ($oldValue -cne $newValue) { throw "Reports use different $field; timing is not comparable." }
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
