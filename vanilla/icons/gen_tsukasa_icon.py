#!/usr/bin/env python3
#
# Project Tsukasa - Built-in Tsukasa Emblem Vector Icon Generator
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

import math
import pathlib
import struct

VICO_MAGIC = 0x5649434F
VICO_VERSION = 1

VCMD_END = 0
VCMD_MOVE = 1
VCMD_LINE = 2
VCMD_QUAD = 3
VCMD_CLOSE = 4
VCMD_FILL_SOLID = 5
VCMD_FILL_GRAD = 6
VCMD_STROKE = 7
VCMD_CIRCLE = 8
VCMD_RECT = 9
VCMD_RRECT = 10


def argb_bytes(argb: int) -> bytes:
    return bytes([
        (argb >> 24) & 0xFF,
        (argb >> 16) & 0xFF,
        (argb >> 8) & 0xFF,
        argb & 0xFF,
    ])


def clamp_u8(val: float) -> int:
    iv = int(round(val))
    if iv < 0:
        return 0
    if iv > 255:
        return 255
    return iv


def cmd_circle(cx: int, cy: int, radius: int, argb: int) -> bytes:
    return bytes([VCMD_CIRCLE, cx, cy, radius]) + argb_bytes(argb)


def cmd_move(x: int, y: int) -> bytes:
    return bytes([VCMD_MOVE, x, y])


def cmd_line(x: int, y: int) -> bytes:
    return bytes([VCMD_LINE, x, y])


def cmd_close() -> bytes:
    return bytes([VCMD_CLOSE])


def cmd_fill_solid(argb: int) -> bytes:
    return bytes([VCMD_FILL_SOLID]) + argb_bytes(argb)


def cmd_stroke(width8: int, argb: int) -> bytes:
    return bytes([VCMD_STROKE, width8]) + argb_bytes(argb)


def cmd_end() -> bytes:
    return bytes([VCMD_END])


def build_tsukasa_emblem() -> bytes:
    cmds = []
    cx, cy = 128, 128

    # Palette: Nord Frost medallion rim over Polar Night backdrop
    frost_rim = 0xFF5E81AC
    polar_bg = 0xFF2E3440
    frost_cyan = 0xFF88C0D0
    snow_white = 0xFFECEFF4

    # 1-2. Circular Polar Night medallion with Frost outer rim
    cmds.append(cmd_circle(cx, cy, 116, frost_rim))
    cmds.append(cmd_circle(cx, cy, 100, polar_bg))

    # 3. Three primary 60-degree snowflake axes in Snow Storm
    arm_r = 80
    stroke_w = 18
    for deg in (0, 60, 120):
        rad = math.radians(deg)
        dx = math.sin(rad) * arm_r
        dy = math.cos(rad) * arm_r
        x0 = clamp_u8(cx - dx)
        y0 = clamp_u8(cy - dy)
        x1 = clamp_u8(cx + dx)
        y1 = clamp_u8(cy + dy)
        cmds.append(cmd_move(x0, y0))
        cmds.append(cmd_line(x1, y1))
        cmds.append(cmd_stroke(stroke_w, snow_white))

    # 4. Six Frost Cyan crystal tips near spoke ends for clear snowflake silhouette
    tip_r = 76
    dot_r = 13
    for i in range(6):
        rad = math.radians(i * 60)
        tx = clamp_u8(cx + math.sin(rad) * tip_r)
        ty = clamp_u8(cy - math.cos(rad) * tip_r)
        cmds.append(cmd_circle(tx, ty, dot_r, frost_cyan))

    # 5. Inner hexagonal frost ring / prism core
    hex_r = 40
    cmds.append(cmd_move(clamp_u8(cx), clamp_u8(cy - hex_r)))
    for i in range(1, 6):
        rad = math.radians(i * 60)
        cmds.append(cmd_line(clamp_u8(cx + math.sin(rad) * hex_r),
                             clamp_u8(cy - math.cos(rad) * hex_r)))
    cmds.append(cmd_close())
    cmds.append(cmd_fill_solid(frost_cyan))

    # 6. Crisp Polar Night inner cutout and symmetric bright center jewel
    cmds.append(cmd_circle(cx, cy, 24, polar_bg))
    cmds.append(cmd_circle(cx, cy, 14, snow_white))

    cmds.append(cmd_end())
    cmd_count = len(cmds)
    stream = b"".join(cmds)

    hdr = struct.pack("<IBBBB", VICO_MAGIC, VICO_VERSION, cmd_count, 0, 0)
    return hdr + stream


def main() -> None:
    out_path = pathlib.Path(__file__).resolve().parent / "builtin" / "TSUKASA.VCO"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    blob = build_tsukasa_emblem()
    out_path.write_bytes(blob)
    print(f"Generated {out_path} ({len(blob)} bytes)")


if __name__ == "__main__":
    main()
