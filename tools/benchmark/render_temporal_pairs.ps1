param(
    [Parameter(Mandatory=$true)][string]$ReportPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int[]]$Index=@(),
    [ValidateRange(1,50)][int]$Count=10,
    [string]$Ffmpeg='D:\msys2\ucrt64\bin\ffmpeg.exe'
)
$ErrorActionPreference='Stop'
$report=Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
if(!@($report.results).Count){throw 'The report contains no returned results to review'}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a NEW review directory; evidence must not be overwritten'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$root=(Resolve-Path -LiteralPath $OutputDirectory).Path
$culture=[Globalization.CultureInfo]::InvariantCulture
if(!$Index.Count){$Index=@(0..([Math]::Min($Count,@($report.results).Count)-1))}
$review=@()
foreach($position in $Index) {
    if($position -lt 0 -or $position -ge @($report.results).Count){throw 'Invalid returned-array position'}
    $pair=$report.results[$position]
    $filters=@();$inputs=@()
    foreach($side in @('left','right')) {
        $start=[double]$pair.($side+'Start');$end=[double]$pair.($side+'End')
        if(![double]::IsFinite($start) -or ![double]::IsFinite($end) -or $start -lt 0 -or $end -le $start){throw 'Invalid matched range'}
        $duration=$end-$start
        $startText=$start.ToString('0.########',$culture)
        $endText=$end.ToString('0.########',$culture)
        $durationText=($duration+.04).ToString('0.########',$culture)
        $interval=([Math]::Max(.04,$duration/6)).ToString('0.########',$culture)
        $inputs+=@('-ss',$startText,'-t',$durationText,'-threads','2','-i',$pair.($side+'Source'))
        $stream=if($side -eq 'left'){0}else{1}
        # Show actual source PTS BEFORE resetting time for sheet alignment.
        # At most six sampled frames; this is a screening aid. Ambiguous or
        # brief events require denser temporal review and track overlays.
        $filters+="[$stream`:v]trim=end=$endText,select='isnan(prev_selected_t)+gte(t-prev_selected_t,$interval)',"+
            "scale=320:180:force_original_aspect_ratio=decrease,pad=320:202:(ow-iw)/2:0:color=black,"+
            "drawtext=fontfile='C\:/Windows/Fonts/consola.ttf':text='%{pts\:hms}':fontcolor=white:fontsize=16:x=5:y=183,"+
            "setpts=PTS-STARTPTS,tile=6x1:padding=3:margin=3:color=0x161b22[$side]"
    }
    $label="Returned index $position | source A above, source B below | inspect matched tracks separately"
    $filter=($filters -join ';')+";[left][right]vstack=inputs=2:shortest=1,pad=ceil(iw/2)*2:ceil((ih+28)/2)*2:0:28:color=0x161b22,"+
        "drawtext=fontfile='C\:/Windows/Fonts/consola.ttf':text='$label':fontcolor=white:fontsize=17:x=5:y=5[out]"
    $output=Join-Path $root ('pair-{0:D3}.jpg' -f $position)
    $arguments=@('-hide_banner','-loglevel','error','-nostdin','-n','-copyts')+$inputs+
        @('-filter_complex',$filter,'-map','[out]','-an','-sn','-frames:v','1','-q:v','2',$output)
    & $Ffmpeg @arguments 2>(Join-Path $root ('pair-{0:D3}.log' -f $position))
    if($LASTEXITCODE -ne 0){throw "Temporal pair extraction failed at returned index $position"}
    $review+=@{returnedArrayIndex=$position;resultId=$pair.id;leftSource=$pair.leftSource;rightSource=$pair.rightSource;
        leftTrackId=$pair.leftTrackId;rightTrackId=$pair.rightTrackId;leftStart=$pair.leftStart;leftEnd=$pair.leftEnd;
        rightStart=$pair.rightStart;rightEnd=$pair.rightEnd;image=$output;ffmpegArguments=$arguments}
}
@{schemaVersion=1;reportSha256=(Get-FileHash -LiteralPath $ReportPath -Algorithm SHA256).Hash.ToLowerInvariant();
    policy='Selected predictions, not independent ground truth; scores/classifications hidden on images; six temporal samples per side; actor association requires track review';
    matcherIndependent=$false;createdUtc=[DateTime]::UtcNow.ToString('o');pairs=$review} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root provenance.json) -Encoding utf8
