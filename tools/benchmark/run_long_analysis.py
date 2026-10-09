"""Bounded Windows analysis with live logs and resource telemetry, no video copies.

Local reports contain source paths and must remain in ignored build directories.
This measures performance; it does not establish matcher precision or recall.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


class MemoryCounters(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in ('PeakWorkingSetSize', 'WorkingSetSize',
        'QuotaPeakPagedPoolUsage', 'QuotaPagedPoolUsage', 'QuotaPeakNonPagedPoolUsage',
        'QuotaNonPagedPoolUsage', 'PagefileUsage', 'PeakPagefileUsage', 'PrivateUsage')]


class MemoryStatus(ctypes.Structure):
    _fields_ = [('length', wintypes.DWORD), ('load', wintypes.DWORD)] + [
        (name, ctypes.c_ulonglong) for name in ('totalPhysical', 'availablePhysical',
        'totalPageFile', 'availablePageFile', 'totalVirtual', 'availableVirtual', 'extendedVirtual')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'video', 'model', 'reid', 'model-root', 'ort', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=14400)
    parser.add_argument('--max-private-mib', type=int, default=16384)
    parser.add_argument('--min-available-mib', type=int, default=2048)
    parser.add_argument('--decode', choices=('cpu', 'nvdec'))
    parser.add_argument('--provider', default='cuda', choices=('cpu', 'cuda', 'tensorrt', 'dml'))
    parser.add_argument('--warm', action='store_true')
    parser.add_argument('--warm-expanded',action='store_true',help='Diagnostic cache reuse with the same expanded candidate policy as a cold run')
    parser.add_argument('--dump-windows', action='store_true')
    parser.add_argument('--profile-inference', action='store_true', help='Capture pose, face and ReID inference timings')
    parser.add_argument('--gpu-telemetry', action='store_true', help='Sample whole-device NVIDIA utilization (includes other applications)')
    parser.add_argument('--cache',type=Path,help='Existing cache for an explicit warm replay; no video copies')
    parser.add_argument('--default-auxiliary-discovery', action='store_true',
        help='Use application discovery for face/ReID models instead of overriding model roots')
    parser.add_argument('--additional-video',type=Path,action='append',default=[])
    args = parser.parse_args()
    if not 1 <= args.timeout <= 43200:
        parser.error('Timeout outside 1..43200 seconds')
    if args.max_private_mib <= 0 or args.min_available_mib <= 0:
        parser.error('Memory limits must be positive')
    for name in ('exe', 'video', 'model', 'reid', 'ort'):
        if not getattr(args, name).is_file():
            parser.error('Missing ' + name)
    for video in args.additional_video:
        if not video.is_file():parser.error('Missing additional video')
    if args.warm_expanded and not args.warm:
        parser.error('--warm-expanded requires --warm')
    if args.cache and (not args.warm or not args.cache.is_dir()):
        parser.error('An existing --cache requires --warm')
    for name in ('face_detection_yunet_2023mar.onnx','face_recognition_sface_2021dec.onnx'):
        if not (args.model_root / name).is_file():
            parser.error('Model root must contain face models: missing ' + name)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    for name in ('PF_DEBUG_MATCHER', 'PF_DEBUG_POSE', 'PF_DEBUG_INFERENCE',
                 'PF_DEBUG_POSES_JSON', 'PF_DEBUG_APPEARANCE_JSON', 'PF_DEBUG_TRACKS_JSON', 'PF_ORT_PROFILE_ROOT',
                 'PF_AUDIT_REUSE_OBSERVATIONS'):
        env.pop(name, None)
    env.update(PF_MODEL_ROOT=str(args.model_root.resolve()), PF_MODEL_PATH=str(args.model.resolve()),
        PF_REID_MODEL_PATH=str(args.reid.resolve()), PF_ORT_DLL=str(args.ort.resolve()),
        PF_PROVIDER_ROOT=str(args.ort.resolve().parent), PF_PROVIDER=args.provider,
        PF_ANALYSIS_TIMEOUT_SEC=str(args.timeout), PF_ANALYSIS_JSON='1', PF_ANALYSIS_MODE='combined',
        PF_QUALITY_PROFILE='fast', PF_EXPANDED_SEARCH='0' if args.warm and not args.warm_expanded else '1',
        PF_DEBUG_ANALYSIS='1', QT_QPA_PLATFORM='windows',
        PF_BENCHMARK_CACHE_ROOT=str(args.cache.resolve() if args.cache else out/'cache'))
    if args.warm_expanded:env['PF_AUDIT_REUSE_OBSERVATIONS']='1'
    if args.default_auxiliary_discovery:
        env.pop('PF_MODEL_ROOT', None)
        env.pop('PF_REID_MODEL_PATH', None)
    # cuDNN loads its graph DLL dynamically after ORT initialization. Include
    # the explicitly selected bundle for this child, without changing the
    # user's system PATH or copying GPU DLLs into the build directory.
    env['PATH'] = os.pathsep.join((str(args.ort.resolve().parent),
                                  str(args.exe.resolve().parent), env.get('PATH', '')))
    if args.profile_inference:env['PF_DEBUG_INFERENCE']='1'
    if args.decode:
        env['PF_VIDEO_DECODE'] = args.decode
    if args.dump_windows:
        env['PF_DEBUG_POSES_JSON'] = str(out/'windows.json')
    manifest = dict(source=str(args.video.resolve()), sourceBytes=args.video.stat().st_size,
        executableSha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        configuration={k:v for k,v in env.items() if k.startswith('PF_')}, accuracyBenchmark=False)
    manifest['sources']=[dict(source=str(v.resolve()),bytes=v.stat().st_size) for v in [args.video,*args.additional_video]]
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    kernel.GlobalMemoryStatusEx.argtypes = [ctypes.POINTER(MemoryStatus)]
    clock = time.perf_counter()
    peak = private_peak = samples = 0
    failure = None
    previous_cpu = previous_seconds = 0.
    smi = shutil.which('nvidia-smi') if args.gpu_telemetry else None
    with (out/'stdout.log').open('wb') as stdout, (out/'stderr.log').open('wb') as stderr, \
            (out/'resources.jsonl').open('w', encoding='utf-8', buffering=1) as telemetry:
        proc = subprocess.Popen([str(args.exe.resolve()), '--pf-analysis-smoke', *[str(v.resolve()) for v in [args.video,*args.additional_video]]],
            env=env, stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
        print(json.dumps(dict(pid=proc.pid, output=str(out))), flush=True)
        while proc.poll() is None:
            elapsed = time.perf_counter()-clock
            counters = MemoryCounters()
            counters.cb = ctypes.sizeof(counters)
            ok = psapi.GetProcessMemoryInfo(int(proc._handle), ctypes.byref(counters), counters.cb)
            disk = shutil.disk_usage(out)
            times = [wintypes.FILETIME() for _ in range(4)]
            time_ok = kernel.GetProcessTimes(int(proc._handle), *[ctypes.byref(t) for t in times])
            memory_status = MemoryStatus()
            memory_status.length = ctypes.sizeof(memory_status)
            memory_ok = kernel.GlobalMemoryStatusEx(ctypes.byref(memory_status))
            cpu = sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]) / 1e7 if time_ok else None
            core_usage = (cpu - previous_cpu) / max(elapsed - previous_seconds, .001) if time_ok else None
            if time_ok:
                previous_cpu, previous_seconds = cpu, elapsed
            if ok:
                samples += 1
                peak = max(peak, counters.PeakWorkingSetSize)
                private_peak = max(private_peak, counters.PrivateUsage)
            gpu = None
            if smi:
                try:
                    query = subprocess.run([smi, '--query-gpu=utilization.gpu,memory.used,memory.total',
                        '--format=csv,noheader,nounits'], capture_output=True, text=True, timeout=1,
                        creationflags=subprocess.CREATE_NO_WINDOW)
                    if query.returncode == 0:
                        gpu = [dict(utilizationPercent=float(values[0]), memoryUsedMiB=float(values[1]),
                            memoryTotalMiB=float(values[2]), scope='whole-device')
                            for values in (line.split(',') for line in query.stdout.strip().splitlines())]
                except (OSError, subprocess.TimeoutExpired, ValueError, IndexError):
                    pass
            telemetry.write(json.dumps(dict(seconds=round(elapsed, 3),
                rssBytes=counters.WorkingSetSize if ok else None,
                privateBytes=counters.PrivateUsage if ok else None, diskFreeBytes=disk.free,
                availablePhysicalBytes=memory_status.availablePhysical if memory_ok else None,
                availableCommitBytes=memory_status.availablePageFile if memory_ok else None,
                cpuSeconds=cpu, cpuCoresUsed=core_usage, gpu=gpu))+'\n')
            if elapsed > args.timeout+45:
                failure = 'timeout'
            elif disk.free < 2*1024**3:
                failure = 'disk reserve reached'
            elif ok and counters.PrivateUsage > args.max_private_mib * 1024**2:
                failure = 'test process private-memory limit reached'
            elif memory_ok and min(memory_status.availablePhysical, memory_status.availablePageFile) < args.min_available_mib * 1024**2:
                failure = 'system memory reserve reached'
            if failure:
                proc.kill()
                break
            time.sleep(2)
        code = proc.wait()
    stdout = (out/'stdout.log').read_text(encoding='utf-8', errors='replace')
    match = re.search(r'^\s*results_json\s*:\s*(.+)$', stdout, re.M)
    results = json.loads(match[1]) if match else []
    summary = dict(exitCode=code, failure=failure, elapsedSeconds=time.perf_counter()-clock,
        # A process that finishes before a second poll has only a startup
        # sample. Do not publish that tiny value as its analysis-memory peak.
        peakRssBytes=peak if samples>=2 else None,
        peakPrivateBytes=private_peak if samples>=2 else None,
        resourceSamples=samples, resourcePollingSeconds=2, privatePeakIsSampled=True, results=results,
        completed=code == 0 and match is not None)
    (out/'report.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in summary.items() if k != 'results'}, ensure_ascii=False), flush=True)
    if not summary['completed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
