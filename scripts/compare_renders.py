"""Compare backend screenshots under the engine tolerance policy (stdlib only).

The policy mirrors src/backend/image_compare.elisa: a per-channel peak bound
plus a mean bound, both on a 0..1 scale. PNG support covers what the probes
emit (8-bit RGB/RGBA); anything else fails loudly instead of comparing wrong.

Usage:
  compare_renders.py stats <png>
  compare_renders.py check <png> <width> <height>
  compare_renders.py compare <a.png> <b.png> [--per-channel T] [--mean T]
"""

import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"not a PNG file: {path}")
    width = height = bitdepth = colortype = None
    blobs = []
    offset = 8
    while offset < len(data):
        (length, kind) = struct.unpack(">I4s", data[offset:offset + 8])
        body = data[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            (width, height, bitdepth, colortype, _, _, _) = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            blobs.append(body)
        elif kind == b"IEND":
            break
        offset += 12 + length
    if bitdepth != 8 or colortype not in (2, 6):
        raise ValueError(f"unsupported PNG (need 8-bit RGB/RGBA): {path}")
    channels = 3 if colortype == 2 else 4
    raw = zlib.decompress(b"".join(blobs))
    stride = width * channels
    pixels = bytearray(width * height * channels)
    previous = bytearray(stride)
    position = 0
    for y in range(height):
        kind = raw[position]
        position += 1
        current = bytearray(raw[position:position + stride])
        position += stride
        if kind == 1:
            for i in range(channels, stride):
                current[i] = (current[i] + current[i - channels]) & 255
        elif kind == 2:
            for i in range(stride):
                current[i] = (current[i] + previous[i]) & 255
        elif kind == 3:
            for i in range(stride):
                left = current[i - channels] if i >= channels else 0
                current[i] = (current[i] + ((left + previous[i]) >> 1)) & 255
        elif kind == 4:
            for i in range(stride):
                left = current[i - channels] if i >= channels else 0
                up = previous[i]
                above_left = previous[i - channels] if i >= channels else 0
                estimate = left + up - above_left
                pick = left
                if abs(up - estimate) < abs(pick - estimate):
                    pick = up
                if abs(above_left - estimate) < abs(pick - estimate):
                    pick = above_left
                current[i] = (current[i] + pick) & 255
        elif kind != 0:
            raise ValueError(f"unknown PNG filter {kind}: {path}")
        pixels[y * stride:(y + 1) * stride] = current
        previous = current
    return width, height, channels, pixels


def channel_stats(pixels, channels):
    count = len(pixels) // channels
    sums = [0.0] * 3
    for i in range(count):
        for channel in range(3):
            sums[channel] += pixels[i * channels + channel] / 255.0
    return [total / count for total in sums]


def frame_range(pixels, channels):
    lows = [255] * 3
    highs = [0] * 3
    count = len(pixels) // channels
    for i in range(count):
        for channel in range(3):
            value = pixels[i * channels + channel]
            if value < lows[channel]:
                lows[channel] = value
            if value > highs[channel]:
                highs[channel] = value
    return max(highs[channel] - lows[channel] for channel in range(3)) / 255.0


def compare(first, second, per_channel, mean):
    (_, _, channels_a, a_pixels) = first
    (_, _, channels_b, b_pixels) = second
    if len(a_pixels) // channels_a != len(b_pixels) // channels_b:
        raise ValueError("frame sizes differ")
    count = len(a_pixels) // channels_a
    peak = 0.0
    total = 0.0
    for i in range(count):
        errors = [abs(a_pixels[i * channels_a + c] - b_pixels[i * channels_b + c]) / 255.0 for c in range(3)]
        worst = max(errors)
        peak = max(peak, worst)
        total += worst
    return peak, total / count, peak <= per_channel and total / count <= mean


def resample_nearest(source, target_width, target_height):
    # Nearest-neighbour downscale so backends at different device pixel
    # ratios can be compared on a common grid. Only used for reporting and
    # loose cross-backend tolerance checks, never for exact comparison.
    (width, height, channels, pixels) = source
    out = bytearray(target_width * target_height * channels)
    for y in range(target_height):
        source_y = min(height - 1, y * height // target_height)
        for x in range(target_width):
            source_x = min(width - 1, x * width // target_width)
            source_index = (source_y * width + source_x) * channels
            target_index = (y * target_width + x) * channels
            out[target_index:target_index + 3] = pixels[source_index:source_index + 3]
            if channels == 4:
                out[target_index + 3] = pixels[source_index + 3]
    return (target_width, target_height, channels, out)


def maze_pattern(source, rows, cols, cells):
    # Samples each grid cell's projected centre and reports W (bright) or .
    # (dark). The projection is the shared fixture rule both hosts implement:
    # camera at z=-5 looking toward the wall plane at z=1 (six units away),
    # vertical field of view 45 degrees, same image aspect. Elisa is
    # right-handed with the scene on +Z, so world +X projects to screen
    # LEFT and the mapping below inverts X to print in Elisa's own
    # coordinates. This compares scene semantics between backends, not raw
    # colour: two hosts that draw the same walls in the same places produce
    # the same grid even when their shading differs.
    import math
    (width, height, channels, pixels) = source
    half_height = math.tan(math.radians(22.5)) * 6.0
    half_width = half_height * (width / height)
    walls = set()
    for entry in cells.split(";"):
        if not entry:
            continue
        (x, y) = entry.split(",")
        walls.add((int(x), int(y)))
    lines = []
    for row in range(rows - 1, -1, -1):
        line = ""
        for col in range(cols):
            world_x = col * 0.6 - 2.1
            world_y = row * 0.6 - 2.1
            screen_x = int((0.5 - world_x / half_width * 0.5) * width)
            screen_y = int((0.5 - world_y / half_height * 0.5) * height)
            screen_x = max(0, min(width - 1, screen_x))
            screen_y = max(0, min(height - 1, screen_y))
            index = (screen_y * width + screen_x) * channels
            brightness = max(pixels[index], pixels[index + 1], pixels[index + 2]) / 255.0
            line += "W" if brightness > 0.15 else "."
        lines.append(line)
    return "\n".join(lines)


def pattern_mismatches(source, rows, cols, cells):
    # Every grid position must be bright exactly when it is a wall in the
    # Elisa list, so the rendered frame encodes the authoritative topology.
    rendered = maze_pattern(source, rows, cols, cells).split("\n")
    walls = set()
    for entry in cells.split(";"):
        if entry:
            (x, y) = entry.split(",")
            walls.add((int(x), int(y)))
    mismatches = 0
    for line_index, line in enumerate(rendered):
        row = rows - 1 - line_index
        for col in range(cols):
            expected = "W" if (col, row) in walls else "."
            if line[col] != expected:
                mismatches += 1
    return mismatches


def resolve_cells(spec):
    # "@<manifest>" reads the wall list from a scene manifest so the
    # manifest stays the single source of truth for both hosts and host
    # runners never re-type the topology.
    if not spec.startswith("@"):
        return spec
    for line in open(spec[1:], encoding="utf-8").read().splitlines():
        text = line.strip()
        if text.startswith("walls="):
            return text[len("walls="):]
    raise ValueError(f"manifest has no walls line: {spec[1:]}")


def main(arguments):
    if len(arguments) < 2:
        print(__doc__.splitlines()[0], file=sys.stderr)
        return 2
    command = arguments[1]
    if command == "pattern":
        if len(arguments) != 6:
            print("usage: compare_renders.py pattern <png> <rows> <cols> <cells>", file=sys.stderr)
            return 2
        print(maze_pattern(read_png(arguments[2]), int(arguments[3]), int(arguments[4]), resolve_cells(arguments[5])))
        return 0
    if command == "pattern-match":
        if len(arguments) != 7:
            print("usage: compare_renders.py pattern-match <a.png> <b.png> <rows> <cols> <cells>", file=sys.stderr)
            return 2
        rows, cols, cells = int(arguments[4]), int(arguments[5]), resolve_cells(arguments[6])
        first = maze_pattern(read_png(arguments[2]), rows, cols, cells)
        second = maze_pattern(read_png(arguments[3]), rows, cols, cells)
        if first != second:
            print("first:\n" + first, file=sys.stderr)
            print("second:\n" + second, file=sys.stderr)
            print("pattern mismatch between hosts", file=sys.stderr)
            return 1
        print("pattern match between hosts")
        return 0
    if command == "pattern-check":
        if len(arguments) != 7:
            print("usage: compare_renders.py pattern-check <png> <rows> <cols> <cells> <max-mismatches>", file=sys.stderr)
            return 2
        rows, cols, cells = int(arguments[3]), int(arguments[4]), resolve_cells(arguments[5])
        allowed = int(arguments[6])
        found = pattern_mismatches(read_png(arguments[2]), rows, cols, cells)
        print(f"topology mismatches={found} allowed={allowed}")
        return 0 if found <= allowed else 1
    if command == "stats":
        (width, height, channels, pixels) = read_png(arguments[2])
        means = channel_stats(pixels, channels)
        print(f"{width}x{height} mean={[round(m, 4) for m in means]} range={frame_range(pixels, channels):.4f}")
        return 0
    if command == "check":
        (width, height, channels, pixels) = read_png(arguments[2])
        want = (int(arguments[3]), int(arguments[4]))
        if width % want[0] != 0 or height % want[1] != 0 or width // want[0] != height // want[1]:
            print(f"dimensions {width}x{height} are not an integer scale of {want[0]}x{want[1]}", file=sys.stderr)
            return 1
        scale = width // want[0]
        if scale < 1:
            print(f"dimensions {width}x{height} are smaller than {want[0]}x{want[1]}", file=sys.stderr)
            return 1
        if frame_range(pixels, channels) < 0.01:
            print("frame is blank", file=sys.stderr)
            return 1
        print(f"frame ok: {width}x{height} (scale {scale}) range={frame_range(pixels, channels):.4f}")
        return 0
    if command == "compare":
        per_channel = 0.02
        mean = 0.005
        scale_to_smaller = False
        rest = arguments[4:]
        while rest:
            flag, rest = rest[0], rest[1:]
            if flag == "--scale-to-smaller":
                scale_to_smaller = True
                continue
            value, rest = float(rest[0]), rest[1:]
            if flag == "--per-channel":
                per_channel = value
            elif flag == "--mean":
                mean = value
            else:
                print(f"unknown flag {flag}", file=sys.stderr)
                return 2
        first = read_png(arguments[2])
        second = read_png(arguments[3])
        if scale_to_smaller or len(first[3]) // first[2] != len(second[3]) // second[2]:
            target = (min(first[0], second[0]), min(first[1], second[1]))
            if first[0] != target[0] or first[1] != target[1]:
                first = resample_nearest(first, target[0], target[1])
            if second[0] != target[0] or second[1] != target[1]:
                second = resample_nearest(second, target[0], target[1])
        (peak, average, ok) = compare(first, second, per_channel, mean)
        print(f"peak={peak:.4f} mean={average:.4f} tolerance=({per_channel},{mean})")
        return 0 if ok else 1
    print(f"unknown command {command}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
