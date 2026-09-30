# Run: blender --background --factory-startup --python tools/foot_pivot_reference.py
# Prints the values test/animation_foot_pivot_reference.elisa checks, using
# clean_leg_motion.py's foot-pivot lines verbatim.
import math
import numpy as np
from mathutils import Quaternion, Vector
UP = Vector((0.0, 1.0, 0.0))
EDGE = 8; count = 30
ramp = np.clip(np.arange(count) / EDGE, 0.0, 1.0)
fade = np.minimum(ramp, ramp[::-1]); fade = fade * fade * (3 - 2 * fade)
print("FADE", repr(float(fade[3])), repr(float(fade[26])), repr(float(fade[15])))
GROUND_LOW = 0.015; GROUND_HIGH = 0.04
print("GROUND", repr(float(np.clip((GROUND_HIGH - 0.02) / (GROUND_HIGH - GROUND_LOW), 0.0, 1.0))))
foot0 = Quaternion(Vector((0.1, 1.0, 0.2)).normalized(), 0.3)
ankle0 = Vector((0.1, 0.09, 0.02)); ball0 = Vector((0.12, 0.02, 0.16))
heel_local = foot0.inverted() @ Vector((0.0, ball0.y - ankle0.y, 0.0))
foot = Quaternion(Vector((1.0, 0.2, 0.1)).normalized(), 0.25) @ foot0
ankle = Vector((0.11, 0.095, 0.03)); ball = Vector((0.13, 0.021, 0.17))
heel = ankle + foot @ heel_local
lowest = min(ball.y, heel.y)
wb = math.exp(-(ball.y - lowest) / 0.01); wh = math.exp(-(heel.y - lowest) / 0.01)
pivot = (ball * wb + heel * wh) / (wb + wh)
delta = Quaternion(Vector((1.0, 0.0, 0.3)).normalized(), -0.2)
moved = pivot + delta @ (ankle - pivot)
low_new = min((moved + delta @ (ball - ankle)).y, (moved + delta @ (heel - ankle)).y)
new_ankle = moved + UP * ((lowest - low_new) * 0.8)
v = lambda p: f"{p.x!r} {p.y!r} {p.z!r}"
print("HEEL", v(heel)); print("PIVOT", v(pivot)); print("ANKLE", v(new_ankle))
