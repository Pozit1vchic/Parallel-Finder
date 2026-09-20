param([Parameter(Mandatory = $true)][string]$Destination)
$ErrorActionPreference = 'Stop'
[void](New-Item -ItemType Directory -Force -Path $Destination)
$assets = @(
    @{Folder='face_detection_yunet'; Name='face_detection_yunet_2023mar.onnx'; Hash='8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4'},
    @{Folder='face_recognition_sface'; Name='face_recognition_sface_2021dec.onnx'; Hash='0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79'}
)
foreach ($asset in $assets) {
    $path = Join-Path $Destination $asset.Name
    if (Test-Path -LiteralPath $path) {
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $asset.Hash) { throw "Existing model hash mismatch: $path" }
        continue
    }
    $temporary = $path + '.' + [guid]::NewGuid().ToString('N') + '.part'
    Invoke-WebRequest -Uri ('https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/' + $asset.Folder + '/' + $asset.Name) -OutFile $temporary
    if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $asset.Hash) { throw "Downloaded model hash mismatch: $temporary" }
    Move-Item -LiteralPath $temporary -Destination $path
    Invoke-WebRequest -Uri ('https://raw.githubusercontent.com/opencv/opencv_zoo/main/models/' + $asset.Folder + '/LICENSE') -OutFile (Join-Path $Destination ($asset.Folder + '.LICENSE'))
    Write-Host "Verified: $path"
}
