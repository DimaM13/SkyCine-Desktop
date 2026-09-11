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
mode = "auto"
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
            if sdata.get("mode"):
                mode = str(sdata["mode"])
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

# 2. Factor and bypass logic
is_3x = ("3x" in mode)
factor = 3 if is_3x else 2

if is_3x and fps >= 40.0:
    clip.set_output()
elif (not is_3x) and fps >= 55.0:
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

    # 4. Determine optical flow resolution
    res_map = {
        "lite_2x": 360,
        "lite_3x": 360,
        "balanced_2x": 540,
        "balanced_3x": 540,
        "high_2x": 720,
        "high_3x": 720,
        "ultra_2x": 1080,
        "ultra_3x": 1080
    }


    # If dynamic auto or explicit resolution is provided in current_playback.json:
    target_res = None
    try:
        if sdata.get("target_res"):
            target_res = int(sdata["target_res"])
    except Exception:
        pass

    if not target_res:
        target_res = res_map.get(mode, 540 if factor == 2 else 480)

    # Resolution cap: Optical flow resolution must NEVER exceed the actual video height (never upscale!)
    target_res = min(target_res, base_h)

    # In 4K mode, maximum flow resolution is capped to 720p for stability
    if is_4k and target_res > 720:
        target_res = 720

    # Dynamic scale factor proportional to the video's actual resolution
    scale_factor = min(1.0, float(target_res) / float(base_h))

    # 5. Compute target width & height strictly aligned to multiples of 32!
    # Tensor cores require dimensions to be divisible by 32 for maximum hardware throughput.
    target_w = max(64, int(round((base_w * scale_factor) / 32)) * 32)
    target_h = max(64, int(round((base_h * scale_factor) / 32)) * 32)

    # 6. Downscale to fast RGBS for RIFE optical flow
    low_rgb = core.resize.Bicubic(base_clip, width=target_w, height=target_h, format=vs.RGBS, matrix_in_s="709")

    # 7. Ultra-fast hardware scene cut detection:
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

    # 8. RIFE motion interpolation on GPU with active scene cut protection
    interpolated_rgb = core.rife.RIFE(
        low_rgb_sc,
        model_path=model_path,
        factor_num=factor,
        sc=1,
        gpu_thread=2
    )

    # 9. Interleave preserving 100% native untouched original frames!
    if factor == 3:
        gen1 = core.std.SelectEvery(interpolated_rgb, cycle=3, offsets=1)
        gen2 = core.std.SelectEvery(interpolated_rgb, cycle=3, offsets=2)
        gen1_native = core.resize.Bicubic(gen1, width=base_w, height=base_h, format=base_clip.format, matrix_s="709")
        gen2_native = core.resize.Bicubic(gen2, width=base_w, height=base_h, format=base_clip.format, matrix_s="709")
        # Every 3rd frame is 100% native untouched 1080p!
        out = core.std.Interleave([base_clip, gen1_native, gen2_native])
    else:
        gen_odd = core.std.SelectEvery(interpolated_rgb, cycle=2, offsets=1)
        gen_native = core.resize.Bicubic(gen_odd, width=base_w, height=base_h, format=base_clip.format, matrix_s="709")
        # Every 2nd frame is 100% native untouched 1080p!
        out = core.std.Interleave([base_clip, gen_native])

    out.set_output()
