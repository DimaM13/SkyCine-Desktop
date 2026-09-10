import os
import sys
import time
import json

def get_current_gpu_name():
    try:
        import winreg
        key_path = r"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path) as k:
            for i in range(winreg.QueryInfoKey(k)[0]):
                sub_name = winreg.EnumKey(k, i)
                try:
                    with winreg.OpenKey(k, sub_name) as subk:
                        val, _ = winreg.QueryValueEx(subk, "DriverDesc")
                        v_lower = val.lower()
                        if not any(x in v_lower for x in ["virtual", "remote", "teamviewer", "basic display"]):
                            return val
                except Exception:
                    pass
    except Exception:
        pass
    return "Unknown GPU"

def run_benchmark(force=False):
    app_data = os.environ.get("APPDATA", "")
    vs_dir = os.path.join(app_data, "vapoursynth")
    os.makedirs(vs_dir, exist_ok=True)
    bench_file = os.path.join(vs_dir, "rife_benchmark.json")

    gpu_name = get_current_gpu_name()

    if not force and os.path.isfile(bench_file):
        try:
            with open(bench_file, "r", encoding="utf-8") as f:
                data = json.load(f)
            if data.get("gpu") == gpu_name and "timings" in data:
                print(f"[Benchmark] Using cached benchmark for {gpu_name}")
                return data
        except Exception:
            pass

    print(f"[Benchmark] Running RIFE GPU benchmark for: {gpu_name}")

    import vapoursynth as vs
    core = vs.core

    base_dir = os.path.dirname(os.path.abspath(__file__))
    plugin_path = os.path.join(base_dir, "librife_windows_x86-64.dll")
    model_path = os.path.join(base_dir, "models", "rife-v4.6_ensembleFalse")

    core.std.LoadPlugin(plugin_path)

    resolutions = [
        (1080, 1920, 1088),
        (720, 1280, 704),
        (540, 960, 544),
        (480, 864, 480),
        (360, 640, 352)
    ]

    base_clip = core.std.BlankClip(width=1920, height=1080, format=vs.YUV420P8, length=30, fpsnum=24, fpsden=1)

    timings = {}
    for label, tw, th in resolutions:
        try:
            low_rgb = core.resize.Bicubic(base_clip, width=tw, height=th, format=vs.RGBS, matrix_in_s="709")
            r = core.rife.RIFE(low_rgb, model_path=model_path, factor_num=2, sc=0, gpu_thread=2)
            gen_odd = core.std.SelectEvery(r, cycle=2, offsets=1)
            gen_native = core.resize.Bicubic(gen_odd, width=1920, height=1080, format=vs.YUV420P8, matrix_s="709")
            out = core.std.Interleave([base_clip, gen_native])

            # 2 warmup frames
            for i in range(2):
                _ = out.get_frame(i)

            # Measure 6 frames of complete output pipeline
            t0 = time.perf_counter()
            for i in range(2, 8):
                _ = out.get_frame(i)
            dt = (time.perf_counter() - t0) / 6.0 * 1000.0
            timings[str(label)] = round(dt, 2)
            print(f"[Benchmark] {label}p ({tw}x{th}): {dt:.2f} ms ({1000.0/dt:.1f} FPS)")
        except Exception as e:
            print(f"[Benchmark] Error measuring {label}p: {e}")
            timings[str(label)] = 999.0

    result = {
        "gpu": gpu_name,
        "timestamp": int(time.time()),
        "timings": timings
    }

    try:
        with open(bench_file, "w", encoding="utf-8") as f:
            json.dump(result, f, indent=2)
        print(f"[Benchmark] Saved results to {bench_file}")
    except Exception as e:
        print(f"[Benchmark] Error saving benchmark: {e}")

    print(json.dumps(result, indent=2))
    return result

if __name__ == "__main__":
    force_run = "--force" in sys.argv
    run_benchmark(force=force_run)
