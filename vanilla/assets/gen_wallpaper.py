#!/usr/bin/env python3
#
# Project Tsukasa — Procedural Nordic Aurora Desktop Wallpaper Generator
#
# Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
#
# Project Tsukasa was created and is maintained by frosty (@enafrosty).
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version. See the top-level LICENSE file.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#

"""Generate a crisp 1280x800 PNG Nordic Aurora wallpaper for Project Tsukasa."""

import math
import os
import struct
import zlib


def clamp01(v):
    if v < 0.0:
        return 0.0
    if v > 1.0:
        return 1.0
    return v


def lerp_rgb(c0, c1, t):
    t = clamp01(t)
    return (
        int(c0[0] + (c1[0] - c0[0]) * t + 0.5),
        int(c0[1] + (c1[1] - c0[1]) * t + 0.5),
        int(c0[2] + (c1[2] - c0[2]) * t + 0.5),
    )


def generate_png(out_path, width=1280, height=800):
    sky_top = (18, 22, 32)
    sky_mid = (30, 36, 48)
    sky_bot = (42, 50, 66)

    aurora_cyan = (104, 178, 198)
    aurora_teal = (78, 152, 148)
    aurora_violet = (118, 102, 168)

    ridge_far = (32, 38, 52)
    ridge_mid = (25, 30, 41)
    ridge_near = (19, 23, 32)

    rows = bytearray()
    for y in range(height):
        v = y / float(height - 1)
        row = bytearray(1 + width * 3)
        row[0] = 0

        if v < 0.55:
            sky_base = lerp_rgb(sky_top, sky_mid, v / 0.55)
        else:
            sky_base = lerp_rgb(sky_mid, sky_bot, (v - 0.55) / 0.45)

        for x in range(width):
            u = x / float(width - 1)
            r, g, b = sky_base

            # Broad upper-right aurora curtain
            center1 = 0.28 + 0.10 * math.sin(u * 4.2 + 0.4) - 0.08 * u
            d1 = (v - center1) / 0.22
            band1 = math.exp(-d1 * d1) * (0.28 + 0.14 * math.cos(u * 7.5 - 0.6))
            band1 *= clamp01(1.3 - abs(u - 0.58) * 1.4)

            # Secondary lower-left aurora ribbon
            center2 = 0.40 + 0.07 * math.cos(u * 5.0 + 1.1)
            d2 = (v - center2) / 0.16
            band2 = math.exp(-d2 * d2) * (0.18 + 0.08 * math.sin(u * 10.0))
            band2 *= clamp01(1.2 - abs(u - 0.32) * 1.6)

            # Soft violet tint high in the sky
            d3 = (v - (0.18 + 0.05 * math.sin(u * 3.1))) / 0.18
            band3 = math.exp(-d3 * d3) * 0.14 * clamp01(1.2 - abs(u - 0.72) * 1.8)

            r, g, b = lerp_rgb((r, g, b), aurora_cyan, clamp01(band1))
            r, g, b = lerp_rgb((r, g, b), aurora_teal, clamp01(band2))
            r, g, b = lerp_rgb((r, g, b), aurora_violet, clamp01(band3))

            # Layered subtle Nordic mountain ridges along lower third
            h_far = 0.73 + 0.045 * math.sin(u * 5.4 + 0.8) + 0.022 * math.cos(u * 11.3)
            h_mid = 0.80 + 0.040 * math.sin(u * 4.1 + 2.5) + 0.020 * math.sin(u * 9.7)
            h_near = 0.88 + 0.032 * math.cos(u * 3.6 + 0.3) + 0.014 * math.sin(u * 8.2)

            if v >= h_near:
                cov = clamp01((v - h_near) * height * 0.5)
                r, g, b = lerp_rgb((r, g, b), ridge_near, cov)
            elif v >= h_mid:
                cov = clamp01((v - h_mid) * height * 0.5)
                r, g, b = lerp_rgb((r, g, b), ridge_mid, cov)
            elif v >= h_far:
                cov = clamp01((v - h_far) * height * 0.5)
                r, g, b = lerp_rgb((r, g, b), ridge_far, cov)

            idx = 1 + x * 3
            row[idx] = r & 0xFF
            row[idx + 1] = g & 0xFF
            row[idx + 2] = b & 0xFF

        rows.extend(row)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
    png.extend(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
    png.extend(chunk(b"IEND", b""))

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(png)
    print(f"Generated {out_path} ({len(png)} bytes, {width}x{height})")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    generate_png(os.path.join(here, "wallpaper.png"))
