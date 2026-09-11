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
