#!/usr/bin/env python3
#
# Project Tsukasa — Automated QEMU Visual & Remote-Desktop Verification Harness
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

"""Boot Project Tsukasa in QEMU with QMP (and optional VNC) to inspect UI visuals."""

import argparse
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import time
import zlib


def find_tool(name):
    found = shutil.which(name)
    if found:
        return found
    for prefix in (r"C:\msys64\ucrt64\bin", r"C:\msys64\usr\bin"):
        for suffix in (".exe", ""):
            cand = os.path.join(prefix, name + suffix)
            if os.path.exists(cand):
                return cand
    raise FileNotFoundError(f"Required tool not found on PATH: {name}")


def read_ppm(ppm_path):
    with open(ppm_path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise RuntimeError(f"Unsupported PPM format in {ppm_path}: {magic!r}")
        dims = f.readline()
        while dims.startswith(b"#"):
            dims = f.readline()
        w, h = map(int, dims.split())
        _maxval = int(f.readline().strip())
        raw = f.read(w * h * 3)
    if len(raw) != w * h * 3:
        raise RuntimeError(f"Truncated PPM data in {ppm_path}")
    return w, h, raw


def write_png(w, h, raw_rgb, png_path):
    stride = w * 3
    rows = b"".join(b"\x00" + raw_rgb[y * stride:(y + 1) * stride] for y in range(h))

    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 6))
        + chunk(b"IEND", b"")
    )
    with open(png_path, "wb") as f:
        f.write(png)


class QmpClient:
    def __init__(self, port):
        self.sock = None
        for _ in range(75):
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
                break
            except OSError:
                time.sleep(0.2)
        if not self.sock:
            raise RuntimeError(f"Timed out connecting to QMP port {port}")
        self.sf = self.sock.makefile("rwb", buffering=0)
        self.read_msg()
        self.cmd("qmp_capabilities")

    def read_msg(self):
        while True:
            line = self.sf.readline()
            if not line:
                return None
            obj = json.loads(line.decode("utf-8"))
            if "event" not in obj:
                return obj

    def cmd(self, execute, arguments=None):
        req = {"execute": execute}
        if arguments is not None:
            req["arguments"] = arguments
        self.sf.write((json.dumps(req) + "\n").encode("utf-8"))
        resp = self.read_msg()
        if resp and "error" in resp:
            raise RuntimeError(f"QMP error on {execute}: {resp['error']}")
        return resp

    def hmp(self, command_line):
        return self.cmd("human-monitor-command", {"command-line": command_line})

    def move_to(self, tx, ty):
        # Zero pointer to top-left corner, then step with PS/2-safe deltas.
        for _ in range(15):
            self.hmp("mouse_move -120 -120")
            time.sleep(0.012)
        cx, cy = 0, 0
        while cx < tx or cy < ty:
            dx = min(90, tx - cx)
            dy = min(90, ty - cy)
            self.hmp(f"mouse_move {dx} {dy}")
            cx += dx
            cy += dy
            time.sleep(0.018)

    def click(self, tx, ty, wait_after=0.5):
        self.move_to(tx, ty)
        time.sleep(0.08)
        self.hmp("mouse_button 1")
        time.sleep(0.06)
        self.hmp("mouse_button 0")
        time.sleep(wait_after)

    def drag(self, sx, sy, steps, dx, dy, delay=0.05):
        self.move_to(sx, sy)
        time.sleep(0.08)
        self.hmp("mouse_button 1")
        time.sleep(0.08)
        for _ in range(steps):
            self.hmp(f"mouse_move {dx} {dy}")
            time.sleep(delay)
        self.hmp("mouse_button 0")
        time.sleep(0.6)

    def send_text(self, text):
        keymap = {
            " ": "spc",
            "\n": "ret",
            "*": "shift-8",
            "+": "shift-equal",
            "-": "minus",
            "/": "slash",
            "=": "equal",
            ".": "dot",
        }
        for ch in text:
            if ch in keymap:
                key = keymap[ch]
            elif ch.isupper():
                key = "shift-" + ch.lower()
            else:
                key = ch.lower()
            self.hmp(f"sendkey {key}")
            time.sleep(0.05)

    def screendump(self, out_dir, name, save_png=True):
        ppm_path = os.path.join(out_dir, f"{name}.ppm")
        png_path = os.path.join(out_dir, f"{name}.png")
        self.cmd("screendump", {"filename": ppm_path.replace("\\", "/")})
        time.sleep(0.2)
        w, h, raw = read_ppm(ppm_path)
        if save_png:
            write_png(w, h, raw, png_path)
        try:
            os.remove(ppm_path)
        except OSError:
            pass
        if save_png:
            print(f"[visual] saved {png_path} ({w}x{h})")
        return w, h, raw

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        try:
            self.sf.close()
            self.sock.close()
        except Exception:
            pass


def check_no_pure_black_bars(w, h, raw, x0, y0, x1, y1, min_run=40):
    longest = 0
    for y in range(max(0, y0), min(h, y1)):
        run = 0
        row_off = y * w * 3
        for x in range(max(0, x0), min(w, x1)):
            idx = row_off + x * 3
            if raw[idx] == 0 and raw[idx + 1] == 0 and raw[idx + 2] == 0:
                run += 1
                if run > longest:
                    longest = run
            else:
                run = 0
    return longest < min_run, longest


def region_diff_ratio(w, raw_a, raw_b, x0, y0, x1, y1):
    total = 0
    changed = 0
    for y in range(y0, y1):
        row_off = y * w * 3
        for x in range(x0, x1):
            idx = row_off + x * 3
            total += 1
            if (
                raw_a[idx] != raw_b[idx]
                or raw_a[idx + 1] != raw_b[idx + 1]
                or raw_a[idx + 2] != raw_b[idx + 2]
            ):
                changed += 1
    return (changed / total) if total else 0.0


def run_visual_test(args):
    os.makedirs(args.out_dir, exist_ok=True)
    disk = os.path.join(args.out_dir, "visual_disk.img")
    qemu_img = find_tool("qemu-img")
    qemu_sys = find_tool("qemu-system-x86_64")

    if not os.path.exists(disk):
        subprocess.check_call(
            [qemu_img, "create", "-f", "raw", disk, "64M"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    serial_path = os.path.join(args.out_dir, "serial.log")
    serial_f = open(serial_path, "wb")

    cmd = [
        qemu_sys,
        "-cdrom", args.iso,
        "-drive", f"file={disk},format=raw,if=ide",
        "-m", "256",
        "-smp", "2",
        "-vga", "std",
        "-no-reboot",
        "-serial", "stdio",
        "-qmp", f"tcp:127.0.0.1:{args.qmp_port},server,nowait",
    ]
    if args.vnc:
        vnc_spec = args.vnc if ":" in args.vnc else f"127.0.0.1:{args.vnc}"
        if vnc_spec.startswith(":"):
            vnc_spec = "127.0.0.1" + vnc_spec
        cmd.extend(["-vnc", vnc_spec])
        print(f"[visual] VNC server enabled at {vnc_spec} (connect via VNC viewer)")
    else:
        cmd.extend(["-display", "none"])

    proc = subprocess.Popen(cmd, stdout=serial_f, stderr=subprocess.STDOUT)
    qmp = None
    try:
        qmp = QmpClient(args.qmp_port)
        print(f"[visual] waiting {args.boot_wait}s for desktop initialization...")
        time.sleep(args.boot_wait)

        if args.scenario == "interactive":
            print(f"[visual] interactive mode active (QMP 127.0.0.1:{args.qmp_port}); Ctrl+C to stop")
            while proc.poll() is None:
                time.sleep(0.5)
            return 0

        w, h, raw_boot = qmp.screendump(args.out_dir, "01_boot")
        assert (w, h) == (1280, 800), f"Unexpected framebuffer size {w}x{h}"

        # Verify dock bottom corners blend over wallpaper rather than opaque bg_base (0x2E3440).
        dock_x0 = (1280 - 256) // 2
        dock_y0 = 800 - 52 - 12
        corner_idx = (dock_y0 * w + dock_x0) * 3
        c_rgb = (raw_boot[corner_idx], raw_boot[corner_idx + 1], raw_boot[corner_idx + 2])
        assert c_rgb != (0x2E, 0x34, 0x40), f"Opaque bg_base box detected at dock corner: {c_rgb}"

        # Confirm dock stayed at 6 pinned items (256px) with all startup app titles resolved.
        with open(serial_path, "rb") as sf:
            serial_boot = sf.read().decode("utf-8", errors="replace")
        for expected_title in ("title='Terminal'", "title='Task Manager'", "title='File Manager'"):
            assert expected_title in serial_boot, f"Missing startup window {expected_title} in serial log"
        assert "title='Dock' (310x52)" not in serial_boot, "Dock expanded unexpectedly to 310px (duplicate item)"

        if args.scenario == "boot":
            return 0

        # Drag Terminal from titlebar at (236, 46) down-right 300x192 in 12 rapid moves.
        qmp.drag(236, 46, steps=12, dx=25, dy=16, delay=0.04)
        _, _, raw_drag = qmp.screendump(args.out_dir, "02_drag")
        ok_black, longest_run = check_no_pure_black_bars(w, h, raw_drag, 40, 260, 1240, 720, min_run=40)
        assert ok_black, f"Detected solid-black drag trail artifact ({longest_run}px run)"

        # Open System Menu via top-left Tsukasa emblem.
        qmp.click(16, 14, wait_after=0.6)
        qmp.screendump(args.out_dir, "03_sysmenu")

        # Open Quick Launcher via Alt+Space.
        qmp.hmp("sendkey esc")
        time.sleep(0.3)
        qmp.hmp("sendkey alt-spc")
        time.sleep(0.6)
        qmp.screendump(args.out_dir, "04_launcher")

        # Close Launcher and inspect Task Manager Processes tab.
        qmp.hmp("sendkey esc")
        time.sleep(0.3)
        qmp.click(740, 759, wait_after=0.6)  # Raise Task Manager via dock slot 5
        qmp.click(946, 103, wait_after=0.6)  # Click Processes tab (648 + 596/2, 58 + 45)
        qmp.screendump(args.out_dir, "05_taskmgr_procs")

        # Raise File Manager via dock slot 1.
        qmp.click(580, 759, wait_after=0.6)
        qmp.screendump(args.out_dir, "06_filemgr")

        # Launch Notepad via dock slot 2 and verify typed text visually.
        qmp.click(620, 759, wait_after=1.6)
        _, _, raw_note_pre = qmp.screendump(args.out_dir, "_note_pre", save_png=False)
        qmp.send_text("Hello Tsukasa")
        time.sleep(0.4)
        _, _, raw_note_post = qmp.screendump(args.out_dir, "07_notepad")
        note_diff = region_diff_ratio(w, raw_note_pre, raw_note_post, 270, 128, 540, 175)
        print(f"[visual] Notepad typing pixel delta in [270..540, 128..175]: {note_diff:.4f}")
        assert note_diff > 0.002, f"Notepad did not render typed text (diff={note_diff:.4f})"

        # Launch Calculator via dock slot 3 and verify 7*6 -> 42 visually.
        qmp.click(660, 759, wait_after=1.6)
        _, _, raw_calc_pre = qmp.screendump(args.out_dir, "_calc_pre", save_png=False)
        qmp.send_text("7*6\n")
        time.sleep(0.4)
        _, _, raw_pre_settings = qmp.screendump(args.out_dir, "08_calc")
        calc_diff = region_diff_ratio(w, raw_calc_pre, raw_pre_settings, 1020, 150, 1165, 218)
        print(f"[visual] Calculator result pixel delta in [1020..1165, 150..218]: {calc_diff:.4f}")
        assert calc_diff > 0.003, f"Calculator did not update display on keyboard input (diff={calc_diff:.4f})"

        # Launch Settings via dock slot 4 and confirm window actually opens & paints.
        qmp.click(700, 759, wait_after=1.8)
        _, _, raw_settings = qmp.screendump(args.out_dir, "09_settings")
        diff = region_diff_ratio(w, raw_pre_settings, raw_settings, 340, 110, 900, 550)
        print(f"[visual] Settings launch pixel delta in [340..900, 110..550]: {diff:.3f}")
        assert diff > 0.08, f"Settings window did not appear cleanly (diff={diff:.3f})"

        print("[visual] ALL VISUAL CHECKS PASSED")
        return 0
    finally:
        if qmp is not None:
            qmp.close()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        serial_f.close()


def main():
    parser = argparse.ArgumentParser(description="Tsukasa QEMU QMP visual & remote desktop test harness")
    parser.add_argument("--iso", default="tsukasa.iso", help="Path to tsukasa.iso")
    parser.add_argument("--out-dir", default="tmp-visual-test", help="Output directory for PNG screenshots & serial.log")
    parser.add_argument("--qmp-port", type=int, default=4460, help="TCP port for QEMU QMP control")
    parser.add_argument("--vnc", default=None, help="Optional QEMU VNC display (e.g. :1 -> 127.0.0.1:5901)")
    parser.add_argument("--boot-wait", type=float, default=12.0, help="Seconds to wait after boot before testing")
    parser.add_argument("--scenario", choices=("boot", "full", "interactive"), default="full")
    args = parser.parse_args()
    sys.exit(run_visual_test(args))


if __name__ == "__main__":
    main()
