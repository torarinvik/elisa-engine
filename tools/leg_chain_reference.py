# Run: blender --background --factory-startup --python tools/leg_chain_reference.py
# Prints the values test/animation_leg_chain_reference.elisa checks.
# Reference values from clean_leg_motion.py's chain/ankle_wring/knee-turn logic (copied verbatim, closures unrolled).
import math
from mathutils import Matrix, Quaternion, Vector
Y = Vector((0.0, 1.0, 0.0))
def aim(y_world, hinge_world, hinge_local):
    hinge_local = (hinge_local - Y * hinge_local.dot(Y)).normalized()
    hinge_world = (hinge_world - y_world * hinge_world.dot(y_world)).normalized()
    local = Matrix((hinge_local, Y, hinge_local.cross(Y))).transposed()
    target = Matrix((hinge_world, y_world, hinge_world.cross(y_world))).transposed()
    return (target @ local.transposed()).to_quaternion()
hip = Vector((0.0, 1.0, 0.0)); ankle = Vector((0.07, 0.25, 0.12)); upper = 0.45; lower = 0.43
ht = Vector((1.0, 0.1, 0.0)); hs = Vector((0.9, -0.2, 0.1)); straight = Vector((1.0, 0.0, 0.0))
def chain(pole):
    reach = ankle - hip
    distance = min(reach.length, upper + lower - 1e-4)
    axis = reach.normalized()
    bend = (pole - axis * pole.dot(axis)).normalized()
    along = (upper * upper - lower * lower + distance * distance) / (2 * distance)
    k = hip + axis * along + bend * math.sqrt(max(1e-10, upper * upper - along * along))
    target = hip + axis * distance
    n = (k - hip).cross(target - k)
    if n.length < 1e-6:
        n = straight
    return (k, aim((k - hip).normalized(), n, ht), aim((target - k).normalized(), n, hs), (target - ankle).length)
guard = Quaternion(Vector((0.3, 0.2, 1.0)).normalized(), 0.2)
pole0 = Vector((0.1, 0.2, 1.0)).normalized()
k0, th0, sh0, m0 = chain(pole0)
foot = sh0 @ guard @ Quaternion(Vector((0.2, 1.0, 0.1)).normalized(), math.radians(24.0))
def ankle_wring(pole):
    local = guard.inverted() @ (chain(pole)[2].inverted() @ foot)
    flex = math.atan2(local.x, local.w)
    rest = Quaternion((math.cos(flex), math.sin(flex), 0.0, 0.0)).inverted() @ local
    return 2.0 * math.acos(min(1.0, abs(rest.w)))
FREE = math.radians(10.0); SOFT = math.radians(6.0)
now = ankle_wring(pole0)
limit = FREE + SOFT * math.tanh((now - FREE) / SOFT)
axis = (ankle - hip).normalized()
best = (now, 0.0)
for step in range(1, 51):
    found = None
    for sign in (1.0, -1.0):
        angle = sign * step * math.radians(1.0)
        value = ankle_wring(Quaternion(axis, angle) @ pole0)
        best = min(best, (value, angle))
        if value <= limit and found is None:
            found = angle
    if found is not None:
        best = (limit, found); break
def q(x): return f"{x.x!r} {x.y!r} {x.z!r} {x.w!r}"
print("KNEE", repr(k0.x), repr(k0.y), repr(k0.z))
print("THIGH", q(th0)); print("SHIN", q(sh0)); print("MISS", repr(m0))
print("FOOT", q(foot)); print("GUARD", q(guard))
print("WRING", repr(now)); print("LIMIT", repr(limit)); print("TURN", repr(best[1]))
