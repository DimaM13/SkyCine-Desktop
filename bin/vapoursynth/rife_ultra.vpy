import os
import sys
import vapoursynth as vs

core = vs.core

base_dir = os.path.dirname(os.path.abspath(__file__))
plugin_path = os.path.join(base_dir, "librife_windows_x86-64.dll")
model_path = os.path.join(base_dir, "models", "rife-v4.6_ensembleFalse")

core.std.LoadPlugin(plugin_path)

clip = video_in

fps = clip.fps_num / clip.fps_den if clip.fps_den != 0 else 24
if fps >= 45:
    clip.set_output()
else:
    w = clip.width
    h = clip.height

    # Ultra mode: 720p optical flow estimation (high detail, best for RTX 3060+ or light scenes)
    target_h = min(720, h)
    target_w = int(round((w * (target_h / h)) / 2) * 2)

    # Downscale to RGB for optical flow
    low_rgb = core.resize.Bicubic(clip, width=target_w, height=target_h, format=vs.RGBS, matrix_in_s="709")

    # RIFE 2x motion interpolation
    interpolated_rgb = core.rife.RIFE(
        low_rgb,
        model_path=model_path,
        factor_num=2,
        sc=1,
        gpu_thread=2
    )

    # Extract only the generated in-between (odd) frames
    gen_odd = core.std.SelectEvery(interpolated_rgb, cycle=2, offsets=1)

    # Upscale in-between frames back to native resolution and format
    gen_native = core.resize.Bicubic(gen_odd, width=w, height=h, format=clip.format, matrix_s="709")

    # Interleave: even frames are 100% untouched native frames, odd frames are the AI motion frames!
    out = core.std.Interleave([clip, gen_native])
    out.set_output()
