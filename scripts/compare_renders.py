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


def main(arguments):
    if len(arguments) < 2:
        print(__doc__.splitlines()[0], file=sys.stderr)
        return 2
    command = arguments[1]
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
        rest = arguments[4:]
        while rest:
            flag, value, rest = rest[0], float(rest[1]), rest[2:]
            if flag == "--per-channel":
                per_channel = value
            elif flag == "--mean":
                mean = value
            else:
                print(f"unknown flag {flag}", file=sys.stderr)
                return 2
        (peak, average, ok) = compare(read_png(arguments[2]), read_png(arguments[3]), per_channel, mean)
        print(f"peak={peak:.4f} mean={average:.4f} tolerance=({per_channel},{mean})")
        return 0 if ok else 1
    print(f"unknown command {command}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
