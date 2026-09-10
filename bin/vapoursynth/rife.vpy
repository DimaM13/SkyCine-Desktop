import os
import sys
import vapoursynth as vs

core = vs.core

# Load RIFE plugin
base_dir = os.path.dirname(os.path.abspath(__file__))
plugin_path = os.path.join(base_dir, "librife_windows_x86-64.dll")
model_path = os.path.join(base_dir, "models", "rife-v4.6_ensembleFalse")

core.std.LoadPlugin(plugin_path)

clip = video_in

# Check if frame rate is already high (e.g. 50+ fps)
fps = clip.fps_num / clip.fps_den if clip.fps_den != 0 else 24
if fps >= 45:
    # No need to interpolate 50/60fps video
    clip.set_output()
else:
    # Determine mode: balanced (default) downscales 4K to 1080p for smooth real-time 60fps
    mode = os.environ.get("SKYCINE_RIFE_MODE", "balanced").lower()

    target_w = clip.width
    target_h = clip.height

    if mode == "balanced" and (target_w > 1920 or target_h > 1080):
        # Downscale proportionally so max dimension fits 1920x1080
        # Ensure even width/height for YUV/RGB
        scale = min(1920 / target_w, 1080 / target_h)
        target_w = int(round((target_w * scale) / 2) * 2)
        target_h = int(round((target_h * scale) / 2) * 2)
        clip = core.resize.Bicubic(clip, width=target_w, height=target_h)

    # RIFE requires vs.RGBS (32-bit float RGB)
    clip_rgb = core.resize.Bicubic(clip, format=vs.RGBS, matrix_in_s="709")

    # UHD flag for 4K in ultra mode
    is_uhd = 1 if (target_w > 1920 or target_h > 1080) else 0

    # RIFE 2x interpolation with scene-change cut detection (sc=1)
    interpolated_rgb = core.rife.RIFE(
        clip_rgb,
        model_path=model_path,
        factor_num=2,
        sc=1,
        uhd=is_uhd
    )

    # Convert back to original video format
    out_clip = core.resize.Bicubic(interpolated_rgb, format=clip.format, matrix_s="709")
    out_clip.set_output()
