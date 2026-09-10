import os
import sys
import json
import re
from fractions import Fraction
import vapoursynth as vs

core = vs.core

base_dir = os.path.dirname(os.path.abspath(__file__))
plugin_path = os.path.join(base_dir, "librife_windows_x86-64.dll")
model_path = os.path.join(base_dir, "models", "rife-v4.6_ensembleFalse")

core.std.LoadPlugin(plugin_path)

clip = video_in

def to_fraction(fps_val):
    standards = [
        (23.976023976, 24000, 1001),
        (24.0, 24, 1),
        (25.0, 25, 1),
        (29.97002997, 30000, 1001),
        (30.0, 30, 1),
        (50.0, 50, 1),
        (59.94005994, 60000, 1001),
        (60.0, 60, 1)
    ]
    for s_val, num, den in standards:
        if abs(fps_val - s_val) < 0.05:
            return num, den
    try:
        f = Fraction(fps_val).limit_denominator(1001)
        return f.numerator, f.denominator
    except Exception:
        return 24, 1

# 1. Determine actual playback framerate from MPV state file or clip metadata
fps = 0.0
is_4k_hint = False
try:
    app_data = os.environ.get("APPDATA", "")
    state_file = os.path.join(app_data, "vapoursynth", "current_playback.json")
    if os.path.isfile(state_file):
        with open(state_file, "r", encoding="utf-8") as f:
            sdata = json.load(f)
            if sdata.get("fps") and float(sdata["fps"]) > 0:
                fps = float(sdata["fps"])
            if sdata.get("is4K"):
                is_4k_hint = bool(sdata["is4K"])
except Exception:
    pass

if fps <= 0:
    if clip.fps_den != 0 and clip.fps_num != 0:
        fps = clip.fps_num / clip.fps_den
    else:
        fps = 24.0

# Critical: Assign strict, monotonic timestamps via AssumeFPS to eliminate duplicate PTS drops
fps_num, fps_den = to_fraction(fps)
clip = core.std.AssumeFPS(clip, fpsnum=fps_num, fpsden=fps_den)

# 2. Bypass if framerate is already high (>= 45 FPS)
if fps >= 45:
    clip.set_output()
else:
    w = clip.width
    h = clip.height

    # 3. 4K / UHD optimization fallback:
    # If video resolution exceeds 1080p, downscale to a 720p base while strictly preserving
    # the exact aspect ratio and aligning to multiples of 32 for optimal GPU tensor alignment.
    is_4k = (w > 1920 or h > 1080 or is_4k_hint)
    if w > 1920 or h > 1080:
        scale_4k = min(1280.0 / w, 720.0 / h)
        base_w = int(round((w * scale_4k) / 32)) * 32
        base_h = int(round((h * scale_4k) / 32)) * 32
        base_clip = core.resize.Bilinear(clip, width=base_w, height=base_h, format=vs.YUV420P8)
    else:
        base_w = w
        base_h = h
        base_clip = clip

    # 4. Dynamic frame budget calculation based on target display framerate:
    # Target frame interval = 1000.0 / (fps * 2)
    # Total MPV overhead (hwdec copy, nv12->yuv420p swscale, vo/gpu-next render) is ~5.0-5.5ms
    # Usable budget for the RIFE neural network:
    target_fps = fps * 2.0
    frame_interval_ms = 1000.0 / target_fps
    overhead_ms = 5.5
    frame_budget_ms = max(6.0, frame_interval_ms - overhead_ms)

    # Resolution ladder: 1080, 720, 540, 480, 360
    # Pick the highest quality resolution that reliably runs within frame_budget_ms
    scale_factor = None
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
                        # 5% safety margin on benchmark timing
                        effective_t = float(raw_t) * 1.05
                        if effective_t <= frame_budget_ms:
                            scale_factor = s
                            break
    except Exception:
        scale_factor = None

    if scale_factor is None:
        # Fallback scale factor if no benchmark is found
        scale_factor = 0.333 if fps > 25.5 else 0.444

    # 4. Compute target width & height strictly aligned to multiples of 32!
    # Tensor cores require dimensions to be divisible by 32 for maximum hardware throughput.
    target_w = max(64, int(round((base_w * scale_factor) / 32)) * 32)
    target_h = max(64, int(round((base_h * scale_factor) / 32)) * 32)

    # 5. Downscale to fast RGBS for RIFE optical flow
    low_rgb = core.resize.Bicubic(base_clip, width=target_w, height=target_h, format=vs.RGBS, matrix_in_s="709")

    # 6. Ultra-fast hardware scene cut detection:
    # Calculates frame difference between adjacent frames to identify real cuts
    low_rgb_next = low_rgb[1:] + low_rgb[-1]
    stats = core.std.PlaneStats(low_rgb, low_rgb_next, plane=0)

    def detect_sc(n, f):
        fout = f[0].copy()
        diff = f[1].props.get('PlaneStatsDiff', 0.0)
        # Hard cuts between distinct camera shots typically exceed 0.15 difference
        fout.props['_SceneChangeNext'] = 1 if diff > 0.15 else 0
        return fout

    low_rgb_sc = core.std.ModifyFrame(low_rgb, [low_rgb, stats], selector=detect_sc)

    # 7. RIFE 2x motion interpolation on GPU with active scene cut protection
    interpolated_rgb = core.rife.RIFE(
        low_rgb_sc,
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
