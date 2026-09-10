import os
import sys
import json
import re
import vapoursynth as vs

core = vs.core

base_dir = os.path.dirname(os.path.abspath(__file__))
plugin_path = os.path.join(base_dir, "librife_windows_x86-64.dll")
model_path = os.path.join(base_dir, "models", "rife-v4.6_ensembleFalse")

core.std.LoadPlugin(plugin_path)

clip = video_in

# 1. Bypass if framerate is already high (>= 45 FPS)
fps = clip.fps_num / clip.fps_den if clip.fps_den != 0 else 24
if fps >= 45:
    clip.set_output()
else:
    w = clip.width
    h = clip.height

    # 2. 4K / UHD optimization fallback:
    # If video resolution exceeds 1080p, downscale to a 1080p base while strictly preserving
    # the exact aspect ratio and aligning to multiples of 32 for optimal GPU tensor alignment.
    is_4k = (w > 1920 or h > 1080)
    if is_4k:
        scale_4k = min(1920.0 / w, 1080.0 / h)
        base_w = int(round((w * scale_4k) / 32)) * 32
        base_h = int(round((h * scale_4k) / 32)) * 32
        base_clip = core.resize.Bilinear(clip, width=base_w, height=base_h, format=vs.YUV420P8)
    else:
        base_w = w
        base_h = h
        base_clip = clip

    # 3. Determine optimal optical flow scale factor based on frame rate and benchmark
    # Target frame budgets (allowing 2-3ms headroom for GPU video output and display sync):
    # 24 FPS (48 FPS display): 20.8ms interval -> budget 18.0ms.
    # 30 FPS (60 FPS display): 16.6ms interval -> budget 14.5ms.
    frame_budget_ms = 14.5 if (is_4k or fps > 25.5) else 18.0

    # Scale factor ladder:
    # 720p eq: 0.667 | 540p eq: 0.500 | 480p eq: 0.444 | 360p eq: 0.333
    scale_factor = 0.333
    try:
        app_data = os.environ.get("APPDATA", "")
        bench_file = os.path.join(app_data, "vapoursynth", "rife_benchmark.json")
        if os.path.isfile(bench_file):
            with open(bench_file, "r", encoding="utf-8") as f:
                bench_data = json.load(f)
            timings = bench_data.get("timings", {})
            ladder = [
                (1080, 1.0),
                (720, 0.667),
                (540, 0.500),
                (480, 0.444),
                (360, 0.333)
            ]
            for res_h, s in ladder:
                if (not is_4k) or res_h <= 720:
                    raw_t = timings.get(str(res_h))
                    if raw_t is not None:
                        # Benchmark measures complete pipeline; add 5% safety margin
                        effective_t = float(raw_t) * 1.05
                        if effective_t <= frame_budget_ms:
                            scale_factor = s
                            break
    except Exception:
        scale_factor = 0.333

    if scale_factor is None:
        # Fallback scale factor
        scale_factor = 0.444 if (is_4k or fps > 25.5) else 0.500

    # 4. Compute target width & height strictly aligned to multiples of 32!
    # Tensor cores require dimensions to be divisible by 32 for maximum hardware throughput.
    target_w = max(64, int(round((base_w * scale_factor) / 32)) * 32)
    target_h = max(64, int(round((base_h * scale_factor) / 32)) * 32)

    # 5. Downscale to fast RGBS for RIFE optical flow
    low_rgb = core.resize.Bicubic(base_clip, width=target_w, height=target_h, format=vs.RGBS, matrix_in_s="709")

    # 6. RIFE 2x motion interpolation on GPU
    interpolated_rgb = core.rife.RIFE(
        low_rgb,
        model_path=model_path,
        factor_num=2,
        sc=1,
        gpu_thread=2
    )

    # 7. Extract generated in-between (odd) frames
    gen_odd = core.std.SelectEvery(interpolated_rgb, cycle=2, offsets=1)

    # 8. Upscale in-between frames back to base resolution
    gen_native = core.resize.Bicubic(gen_odd, width=base_w, height=base_h, format=base_clip.format, matrix_s="709")

    # 9. Interleave:
    # Even frames (0, 2, 4...) are 100% untouched base frames (zero quality loss / zero blur),
    # Odd frames (1, 3, 5...) are the AI motion frames!
    out = core.std.Interleave([base_clip, gen_native])
    out.set_output()
