# Model export

The repository does not contain model weights. Export the pinned YOLO26m-pose
asset into the external D: drive bundle with Python 3.10:

```powershell
$env:PYTHONPATH = 'D:\PythonLibs'
$env:PF_MODEL_OUTPUT = 'D:\PF_CUDA\models'
& 'C:\Users\<user>\AppData\Local\Programs\Python\Python310\python.exe' `
  tools\model_export\export_yolo26m_pose.py
```

`D:\PythonLibs` is expected to provide the already installed Ultralytics and
PyTorch packages. The script exports a static batch-1, 640x640, opset-17 ONNX
graph without embedded NMS. The resulting file is
`D:\PF_CUDA\models\yolo26m-pose-640-b1.onnx` and its checked-in metadata is
`models/yolo26m-pose-640-b1.json`.

The checkpoint and ONNX asset are AGPL-3.0 material and therefore stay outside
the Git repository.

For the GPU tier, export the fixed batch-8 asset with the same script:

```powershell
$env:PF_MODEL_BATCH = '8'
& 'C:\Users\<user>\AppData\Local\Programs\Python\Python310\python.exe' `
  tools\model_export\export_yolo26m_pose.py
```

The runtime profile name must match the asset (`b1`, `b8`, or `b16`). The
script prints the exact size and SHA-256 values for the release manifest.
