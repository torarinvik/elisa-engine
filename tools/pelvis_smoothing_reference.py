# Run: python3 tools/pelvis_smoothing_reference.py
# Prints the values test/animation_pelvis_smoothing_reference.elisa checks,
# using clean_leg_motion.py's smooth() and pelvis lines verbatim.
import numpy as np
def smooth(values, sigma, wrap):
    radius = int(3 * sigma)
    kernel = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    kernel /= kernel.sum()
    padded = np.pad(values, [(radius, radius)], mode="wrap" if wrap else "edge")
    return np.convolve(padded, kernel, mode="valid")
count = 40; EDGE = 8
t = np.arange(count)
height = 0.95 + 0.03 * np.sin(t * 2 * np.pi / 6.0) - 0.002 * t
path = 0.1 * np.sin(t / 5.0) + np.where(t > 20, 0.05, 0.0)
for wrap in (False, True):
    fade = np.ones(count)
    if not wrap:
        ramp = np.clip(np.arange(count) / EDGE, 0.0, 1.0)
        fade = np.minimum(ramp, ramp[::-1]); fade = fade * fade * (3 - 2 * fade)
    out = path + (smooth(path, 3.0, wrap) - path) * fade
    drop = smooth(np.minimum(smooth(height, 8.0, wrap) - height, 0.0), 3.0, wrap) * fade
    print("WRAP" if wrap else "EDGE", " ".join(repr(float(out[i])) for i in (3, 20, 21, 36)), "|", " ".join(repr(float(drop[i])) for i in (3, 10, 19, 36)))
