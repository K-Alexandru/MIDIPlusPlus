"""Draws ui/QuartzMIDI.ico: a white quartz prism on the skin's accent blue.

Pure Python so it runs on any machine with the interpreter alone. The
shape is rasterised at each size rather than downsampled, so the 16px entry
is drawn for 16px. Run from anywhere: python tools/gen-icon.py
"""
import math
import os
import struct
import zlib

BLUE = (0x0B, 0x6E, 0xC4)
SIZES = (16, 24, 32, 48, 64, 256)
SUB = 4  # supersampling per axis


def rounded_rect_coverage(x, y, size, radius):
    # Signed distance to a rounded square filling the canvas, in pixels.
    half = size / 2
    qx, qy = abs(x - half) - (half - radius), abs(y - half) - (half - radius)
    outside = math.hypot(max(qx, 0), max(qy, 0))
    inside = min(max(qx, qy), 0)
    return outside + inside - radius


def inside_polygon(x, y, points):
    result = False
    n = len(points)
    for i in range(n):
        x1, y1 = points[i]
        x2, y2 = points[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            result = not result
    return result


def render(size):
    radius = size * 0.22
    s = size
    # The prism: a tall hexagon, pointed at both ends, with the right facet
    # a shade darker so it reads as a solid rather than a flat outline.
    cx, top, bottom, w, tip = s * 0.5, s * 0.14, s * 0.86, s * 0.20, s * 0.20
    left = [(cx, top), (cx, bottom), (cx - w, bottom - tip), (cx - w, top + tip)]
    right = [(cx, top), (cx + w, top + tip), (cx + w, bottom - tip), (cx, bottom)]
    pixels = []
    for py in range(s):
        row = []
        for px in range(s):
            back = 0.0
            fl = 0.0
            fr = 0.0
            for sy in range(SUB):
                for sx in range(SUB):
                    x, y = px + (sx + 0.5) / SUB, py + (sy + 0.5) / SUB
                    d = rounded_rect_coverage(x, y, s, radius)
                    if d < 0:
                        back += 1
                        if inside_polygon(x, y, left): fl += 1
                        elif inside_polygon(x, y, right): fr += 1
            n = SUB * SUB
            back /= n; fl /= n; fr /= n
            # Composite: blue, then white and the facet shade over it.
            r, g, b = BLUE
            shade = 0.82
            r = r * (1 - fl - fr) + 255 * fl + 255 * shade * fr
            g = g * (1 - fl - fr) + 255 * fl + 255 * shade * fr
            b = b * (1 - fl - fr) + 255 * fl + 255 * shade * fr
            row.append((int(round(r)), int(round(g)), int(round(b)), int(round(back * 255))))
        pixels.append(row)
    return pixels


def png(pixels):
    size = len(pixels)
    raw = b''.join(b'\0' + b''.join(struct.pack('BBBB', *p) for p in row) for row in pixels)
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def bmp(pixels):
    size = len(pixels)
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0)
    body = b''.join(b''.join(struct.pack('BBBB', b, g, r, a) for (r, g, b, a) in row) for row in reversed(pixels))
    stride = ((size + 31) // 32) * 4
    mask = b'\0' * stride * size
    return header + body + mask


def main():
    entries = []
    for size in SIZES:
        pixels = render(size)
        entries.append((size, png(pixels) if size == 256 else bmp(pixels)))
    offset = 6 + 16 * len(entries)
    directory = struct.pack('<HHH', 0, 1, len(entries))
    blobs = b''
    for size, data in entries:
        directory += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset + len(blobs))
        blobs += data
    target = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ui', 'QuartzMIDI.ico')
    with open(target, 'wb') as file:
        file.write(directory + blobs)
    print(target, len(directory + blobs), 'bytes')


if __name__ == '__main__':
    main()
